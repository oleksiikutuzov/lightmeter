#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
#include <BH1750.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>

#ifndef LIGHTMETER_DEBUG
#define LIGHTMETER_DEBUG 0
#endif

#ifndef LIGHTMETER_DEBUG_BAUD
#define LIGHTMETER_DEBUG_BAUD 115200
#endif

#if LIGHTMETER_DEBUG
#define DEBUG_BEGIN(baud) Serial.begin(baud)
#define DEBUG_PRINT(value) Serial.print(value)
#define DEBUG_PRINTLN(value) Serial.println(value)
#define DEBUG_FLUSH() Serial.flush()
#else
#define DEBUG_BEGIN(baud)
#define DEBUG_PRINT(value)
#define DEBUG_PRINTLN(value)
#define DEBUG_FLUSH()
#endif

#define OLED_DC 11
#define OLED_CS 12
#define OLED_CLK 8    // 10
#define OLED_MOSI 9   // 9
#define OLED_RESET 10 // 13
Adafruit_SH1106 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

BH1750 lightMeter;

#define DomeMultiplier 2.17     // Multiplier when using a white translucid Dome covering the lightmeter
#define MeteringButtonPin 7     // Metering button pin
#define PlusButtonPin 3         // Plus button pin
#define MinusButtonPin 4        // Minus button pin
#define ModeButtonPin 5         // Mode button pin
#define MenuButtonPin 6         // ISO button pin
#define MeteringModeButtonPin 2 // Metering Mode (Ambient / Flash)
// #define PowerButtonPin          2

#define MaxISOIndex 35
#define MaxApertureIndex 42
#define MaxTimeIndex 55
#define MaxNDIndex 13
#define MaxFlashMeteringTime 5000UL // ms
#define MaxAutoModeIndex 1
#define HighResolutionSaturationLux 27306.0f
#define LowResolutionSaturationLux 54612.0f

float lux;
float filteredAutoLux;
boolean autoLuxFilterInitialized = false;
boolean SensorOverflow = 0;
boolean Overflow = 0; // Displayed reading was clipped by sensor saturation.
boolean SensorError = 0;

boolean PlusButtonState;         // "+" button state
boolean MinusButtonState;        // "-" button state
boolean MeteringButtonState;     // Metering button state
boolean ModeButtonState;         // Mode button state
boolean MenuButtonState;         // ISO button state
boolean MeteringModeButtonState; // Metering mode button state (Ambient / Flash)

boolean previousPlusButtonState = HIGH;
boolean previousMinusButtonState = HIGH;
boolean previousMeteringButtonState = HIGH;
boolean previousModeButtonState = HIGH;
boolean previousMenuButtonState = HIGH;
boolean previousMeteringModeButtonState = HIGH;
unsigned long lastButtonTime = 0;

boolean ISOMenu = false;
boolean NDMenu = false;
boolean mainScreen = false;
boolean modeMenu = false;
boolean flashMetering = false;
unsigned long flashStartTime = 0;
unsigned long lastFlashSampleTime = 0;

// EEPROM for memory recording
#define ISOIndexAddr 1
#define apertureIndexAddr 2
#define modeIndexAddr 3
#define T_expIndexAddr 4
#define meteringModeAddr 5
#define ndIndexAddr 6
#define autoModeIndexAddr 7

#define defaultApertureIndex 12
#define defaultISOIndex 11
#define defaultModeIndex 0
#define defaultT_expIndex 19

uint8_t ISOIndex = EEPROM.read(ISOIndexAddr);
uint8_t apertureIndex = EEPROM.read(apertureIndexAddr);
uint8_t T_expIndex = EEPROM.read(T_expIndexAddr);
uint8_t modeIndex = EEPROM.read(modeIndexAddr);
uint8_t meteringMode = EEPROM.read(meteringModeAddr);
uint8_t ndIndex = EEPROM.read(ndIndexAddr);
uint8_t autoModeIndex = EEPROM.read(autoModeIndexAddr);

int battVolts;
#define batteryInterval 10000UL
#define BatterySmoothingDivisor 4
#define BatteryHysteresisCentivolts 5
#define BatteryFullCentivolts 390
#define autoModeInterval 300UL // ms
#define autoModeFilterWeight 0.25f
#define autoModeLuxDeadband 0.02f
uint8_t batteryLevel = 0;
unsigned long lastBatteryTime = 0;
unsigned long lastAutoModeTime = 0;
boolean autoMode;
#if LIGHTMETER_DEBUG
int rawBattVolts;
boolean debugMenu = false;
#endif

#include "lightmeter.h"

void setup()
{
    pinMode(PlusButtonPin, INPUT_PULLUP);
    pinMode(MinusButtonPin, INPUT_PULLUP);
    pinMode(MeteringButtonPin, INPUT_PULLUP);
    pinMode(ModeButtonPin, INPUT_PULLUP);
    pinMode(MenuButtonPin, INPUT_PULLUP);
    pinMode(MeteringModeButtonPin, INPUT_PULLUP);
    set_sleep_mode(SLEEP_MODE_IDLE);

    DEBUG_BEGIN(LIGHTMETER_DEBUG_BAUD);
    delay(250);

    debugPrintBattery(updateBatteryVoltage(true));

    Wire.begin();
    boolean lightMeterReady = beginLightMeter(BH1750::ONE_TIME_HIGH_RES_MODE_2);
    // lightMeter.begin(BH1750::ONE_TIME_LOW_RES_MODE); // for low resolution but 16ms light measurement time.

    display.begin(SH1106_SWITCHCAPVCC, 0x3C);
    display.setTextColor(WHITE);
    display.clearDisplay();

    // IF NO MEMORY WAS RECORDED BEFORE, START WITH THIS VALUES otherwise it will read "255"
    if (apertureIndex > MaxApertureIndex)
    {
        apertureIndex = defaultApertureIndex;
    }

    if (ISOIndex > MaxISOIndex)
    {
        ISOIndex = defaultISOIndex;
    }

    if (T_expIndex > MaxTimeIndex)
    {
        T_expIndex = defaultT_expIndex;
    }

    if (modeIndex > 1)
    {
        // Aperture priority. Calculating shutter speed.
        modeIndex = 0;
    }

    if (meteringMode > 1)
    {
        meteringMode = 0;
    }

    if (ndIndex > MaxNDIndex)
    {
        ndIndex = 0;
    }

    if (autoModeIndex > MaxAutoModeIndex)
    {
        autoModeIndex = 0;
    }

    autoMode = autoModeIndex;

    lux = lightMeterReady ? getLux() : 0;
    Overflow = SensorOverflow;
    refresh();

    if (autoMode && meteringMode == 0)
    {
        filteredAutoLux = lux;
        autoLuxFilterInitialized = true;
        configureLightMeter(BH1750::CONTINUOUS_HIGH_RES_MODE_2);
        lastAutoModeTime = millis();
    }

}

void loop()
{
    unsigned long currentTime = millis();

    if (currentTime - lastBatteryTime >= batteryInterval)
    {
        lastBatteryTime = currentTime;
        debugPrintBattery(updateBatteryVoltage(false));

        if (mainScreen)
        {
            refresh();
        }
#if LIGHTMETER_DEBUG
        else if (debugMenu)
        {
            showDebugInfoMenu();
        }
#endif
    }

    readButtons();

    menu();

    if (flashMetering)
    {
        currentTime = millis();
        if (currentTime - flashStartTime >= MaxFlashMeteringTime)
        {
            flashMetering = false;
            refresh();
        }
        else if (currentTime - lastFlashSampleTime >= 16)
        {
            lastFlashSampleTime = currentTime;
            float currentLux = getLux(LowResolutionSaturationLux);
            boolean currentOverflow = SensorOverflow;

            if (currentLux > lux)
            {
                lux = currentLux;
                Overflow = currentOverflow;
                DEBUG_PRINT(F("flash peak lux="));
                DEBUG_PRINTLN(lux);
            }

            if (currentOverflow)
            {
                Overflow = 1;
            }
        }
    }
    else if (MeteringButtonState == LOW)
    {
        // Save setting if Metering button pressed.
        SaveSettings();

        lux = 0;
        Overflow = 0;
        refresh();

        if (meteringMode == 0)
        {
            // Ambient light meter mode.
            DEBUG_PRINTLN(F("metering ambient"));
            boolean configured = configureLightMeter(BH1750::ONE_TIME_HIGH_RES_MODE_2);

            lux = configured ? getLux() : 0;
            Overflow = SensorOverflow;

            refresh();

            if (autoMode)
            {
                filteredAutoLux = lux;
                autoLuxFilterInitialized = true;
                configureLightMeter(BH1750::CONTINUOUS_HIGH_RES_MODE_2);
                lastAutoModeTime = millis();
            }
        }
        else if (meteringMode == 1)
        {
            // Flash light metering
            DEBUG_PRINTLN(F("metering flash"));
            if (configureLightMeter(BH1750::CONTINUOUS_LOW_RES_MODE))
            {
                flashMetering = true;
                flashStartTime = millis();
                lastFlashSampleTime = flashStartTime - 16;
            }
            else
            {
                lux = 0;
                Overflow = 0;
                refresh();
            }
        }
    }
    else if (autoMode && meteringMode == 0 && currentTime - lastAutoModeTime >= autoModeInterval)
    {
        boolean previousSensorError = SensorError;
        float measuredLux = getLux();
        boolean measuredOverflow = SensorOverflow;

        if (SensorError)
        {
            if (!previousSensorError)
            {
                lux = 0;
                Overflow = 0;
                refresh();
            }
        }
        else
        {
            boolean overflowChanged = (Overflow != measuredOverflow);
            Overflow = measuredOverflow;

            if (previousSensorError || updateAutomaticLux(measuredLux) || overflowChanged)
            {
                refresh();
            }
        }

        lastAutoModeTime = millis();
    }

    if (autoMode && meteringMode == 0 && !flashMetering)
    {
        sleep_enable();
        sleep_cpu();
        sleep_disable();
    }
}
