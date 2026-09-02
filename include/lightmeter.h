void outOfrange()
{
    display.println(F("--"));
}

#if LIGHTMETER_DEBUG
void drawDebugBuildMarker()
{
    display.setTextSize(1);
    display.setCursor(110, 57);
    display.print(F("DBG"));
}
#else
#define drawDebugBuildMarker()
#endif

#define ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

int getBandgap(void);

const uint8_t batteryFillTop[] PROGMEM = {0, 6, 5, 3, 1};
const uint8_t batteryFillHeight[] PROGMEM = {0, 2, 3, 5, 7};

static_assert(ARRAY_LENGTH(batteryFillTop) == ARRAY_LENGTH(batteryFillHeight), "battery fill tables must match");

int getBatteryThreshold(uint8_t level)
{
    switch (level)
    {
    case 0:
        return 300;
    case 1:
        return 340;
    case 2:
        return 370;
    default:
        return BatteryFullCentivolts;
    }
}

uint8_t getBatteryLevel(int centivolts)
{
    if (centivolts >= BatteryFullCentivolts)
    {
        return 4;
    }
    if (centivolts > 370)
    {
        return 3;
    }
    if (centivolts > 340)
    {
        return 2;
    }
    if (centivolts > 300)
    {
        return 1;
    }

    return 0;
}

void updateBatteryLevel()
{
    while (batteryLevel < 4 && battVolts >= getBatteryThreshold(batteryLevel))
    {
        batteryLevel++;
    }

    while (batteryLevel > 0 && battVolts <= getBatteryThreshold(batteryLevel - 1) - BatteryHysteresisCentivolts)
    {
        batteryLevel--;
    }
}

int updateBatteryVoltage(boolean initialize)
{
    int measuredBattVolts = getBandgap();
#if LIGHTMETER_DEBUG
    rawBattVolts = measuredBattVolts;
#endif

    if (measuredBattVolts <= 0)
    {
        return measuredBattVolts;
    }

    if (initialize)
    {
        battVolts = measuredBattVolts;
        batteryLevel = getBatteryLevel(battVolts);
    }
    else
    {
        battVolts = (battVolts * (BatterySmoothingDivisor - 1) + measuredBattVolts + BatterySmoothingDivisor / 2) / BatterySmoothingDivisor;
        updateBatteryLevel();
    }

    return measuredBattVolts;
}

void debugPrintBattery(int measuredBattVolts)
{
#if LIGHTMETER_DEBUG
    DEBUG_PRINT(F("bat raw="));
    DEBUG_PRINT((double)measuredBattVolts / 100);
    DEBUG_PRINT(F(" sm="));
    DEBUG_PRINT((double)battVolts / 100);
    DEBUG_PRINT(F(" lvl="));
    DEBUG_PRINTLN(batteryLevel);
    DEBUG_FLUSH();
#else
    (void)measuredBattVolts;
#endif
}

void drawBatteryIndicator()
{
    display.drawRect(122, 1, 6, 8, WHITE);
    display.drawLine(124, 0, 125, 0, WHITE);

    uint8_t level = min(batteryLevel, (uint8_t)(ARRAY_LENGTH(batteryFillTop) - 1));
    uint8_t height = pgm_read_byte(&batteryFillHeight[level]);
    if (height > 0)
    {
        display.fillRect(123, pgm_read_byte(&batteryFillTop[level]), 4, height, WHITE);
    }
}

#if LIGHTMETER_DEBUG
void showDebugInfoMenu()
{
    ISOMenu = false;
    mainScreen = false;
    NDMenu = false;
    modeMenu = false;
    debugMenu = true;

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(F("Debug battery"));

    display.setCursor(0, 14);
    display.print(F("raw "));
    display.print((double)rawBattVolts / 100);
    display.print(F("V"));

    display.setCursor(0, 26);
    display.print(F("smooth "));
    display.print((double)battVolts / 100);
    display.print(F("V"));

    display.setCursor(0, 38);
    display.print(F("level "));
    display.print(batteryLevel);
    display.print(F("/4"));

    display.setCursor(0, 50);
    display.print(F("full "));
    display.print((double)BatteryFullCentivolts / 100);
    display.print(F("V"));

    drawDebugBuildMarker();
    display.display();
}
#endif

void SaveSettings()
{
    // Save lightmeter setting into EEPROM.
    EEPROM.update(ndIndexAddr, ndIndex);
    EEPROM.update(ISOIndexAddr, ISOIndex);
    EEPROM.update(modeIndexAddr, modeIndex);
    EEPROM.update(apertureIndexAddr, apertureIndex);
    EEPROM.update(T_expIndexAddr, T_expIndex);
    EEPROM.update(meteringModeAddr, meteringMode);
    EEPROM.update(autoModeIndexAddr, autoModeIndex);
}

boolean beginLightMeter(BH1750::Mode mode)
{
    if (lightMeter.begin(mode))
    {
        return true;
    }

    DEBUG_PRINT(F("sensor begin failed mode="));
    DEBUG_PRINTLN((uint8_t)mode);
    SensorError = 1;
    SensorOverflow = 0;
    return false;
}

boolean configureLightMeter(BH1750::Mode mode)
{
    if (lightMeter.configure(mode))
    {
        return true;
    }

    DEBUG_PRINT(F("sensor configure failed mode="));
    DEBUG_PRINTLN((uint8_t)mode);
    SensorError = 1;
    SensorOverflow = 0;
    return false;
}

// Returns actual value of Vcc (x 100)
int getBandgap(void)
{
    ADCSRA |= _BV(ADEN);

#if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    // For mega boards
    const long InternalReferenceVoltage = 1115L; // Adjust this value to your boards specific internal BG voltage x1000
    // REFS1 REFS0          --> 0 1, AVcc internal ref. -Selects AVcc reference
    // MUX4 MUX3 MUX2 MUX1 MUX0  --> 11110 1.1V (VBG)         -Selects channel 30, bandgap voltage, to measure
    ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (0 << MUX5) | (1 << MUX4) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);

#else
    // For 168/328 boards
    const long InternalReferenceVoltage = 1056L; // Adjust this value to your boards specific internal BG voltage x1000
    // REFS1 REFS0          --> 0 1, AVcc internal ref. -Selects AVcc external reference
    // MUX3 MUX2 MUX1 MUX0  --> 1110 1.1V (VBG)         -Selects channel 14, bandgap voltage, to measure
    ADMUX = (0 << REFS1) | (1 << REFS0) | (0 << ADLAR) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1) | (0 << MUX0);

#endif

    delay(5); // Let mux/reference settle before the first throwaway conversion.
    ADCSRA |= _BV(ADSC);
    while (((ADCSRA & (1 << ADSC)) != 0))
        ;

    uint16_t adcValue = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        ADCSRA |= _BV(ADSC);
        while (((ADCSRA & (1 << ADSC)) != 0))
            ;
        adcValue += ADC;
    }

    ADCSRA &= ~_BV(ADEN);

    adcValue = (adcValue + 2) / 4;
    if (adcValue == 0)
    {
        return 0;
    }

    return (((InternalReferenceVoltage * 1024L) / adcValue) + 5L) / 10L;
}

/*
  Get light value
*/
float getLux(float saturationLux = HighResolutionSaturationLux)
{
    float lux = lightMeter.readLightLevel(false);

    if (lux < 0)
    {
        DEBUG_PRINTLN(F("sensor error"));
        DEBUG_FLUSH();
        SensorError = 1;
        SensorOverflow = 0;
        return 0;
    }

    SensorError = 0;

    if (lux >= saturationLux)
    {
        // light sensor is overloaded.
        DEBUG_PRINT(F("sensor overflow raw_lux="));
        DEBUG_PRINTLN(lux);
        DEBUG_FLUSH();
        SensorOverflow = 1;
        lux = saturationLux;
    }
    else
    {
        SensorOverflow = 0;
    }

    return lux * DomeMultiplier; // DomeMultiplier = 2.17 (calibration)*/
}

boolean updateAutomaticLux(float measuredLux)
{
    if (!autoLuxFilterInitialized)
    {
        filteredAutoLux = measuredLux;
        autoLuxFilterInitialized = true;
        lux = measuredLux;
        return true;
    }

    filteredAutoLux += autoModeFilterWeight * (measuredLux - filteredAutoLux);

    float changeThreshold = max(1.0f, abs(lux) * autoModeLuxDeadband);
    if (abs(filteredAutoLux - lux) < changeThreshold)
    {
        return false;
    }

    lux = filteredAutoLux;
    return true;
}

float log2(float x)
{
    return log(x) / log(2);
}

float lux2ev(float lux)
{
    return log2(lux / 2.5);
}

const float apertureValues[] PROGMEM = {
    1.0f, 1.1f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f, 2.2f, 2.5f, 2.8f,
    3.2f, 3.5f, 4.0f, 4.5f, 5.0f, 5.6f, 6.3f, 7.1f, 8.0f, 9.0f,
    10.0f, 11.0f, 13.0f, 14.0f, 16.0f, 18.0f, 20.0f, 22.0f, 25.0f, 28.0f,
    32.0f, 36.0f, 40.0f, 45.0f, 51.0f, 57.0f, 64.0f, 72.0f, 80.0f, 90.0f,
    102.0f, 114.0f, 128.0f};

static_assert(ARRAY_LENGTH(apertureValues) == MaxApertureIndex + 1, "apertureValues size must match MaxApertureIndex");

// Return aperture value (1.4, 1.8, 2.0) by index in sequence (0, 1, 2, 3, ...).
float getApertureByIndex(uint8_t indx)
{
    if (indx > MaxApertureIndex)
    {
        indx = 0;
    }

    return pgm_read_float(&apertureValues[indx]);
}

const int32_t isoValues[] PROGMEM = {
    8L, 10L, 12L, 16L, 20L, 25L, 32L, 40L, 50L, 64L, 80L, 100L,
    125L, 160L, 200L, 250L, 320L, 400L, 500L, 640L, 800L, 1000L,
    1250L, 1600L, 2000L, 2500L, 3200L, 4000L, 5000L, 6400L, 8000L,
    10000L, 12500L, 16000L, 20000L, 25000L};

static_assert(ARRAY_LENGTH(isoValues) == MaxISOIndex + 1, "isoValues size must match MaxISOIndex");

// Return ISO value (100, 200, 400, ...) by index in sequence (0, 1, 2, 3, ...).
long getISOByIndex(uint8_t indx)
{
    if (indx > MaxISOIndex)
    {
        indx = 0;
    }

    return pgm_read_dword(&isoValues[indx]);
}

const float timeValues[] PROGMEM = {
    0.0001f, 0.000125f, 0.00015625f, 0.0002f, 0.00025f, 0.000333333f, 0.0004f, 0.0005f,
    0.000666667f, 0.0008f, 0.001f, 0.00125f, 0.0015625f, 0.002f, 0.0025f, 0.003333333f,
    0.004f, 0.005f, 0.006666667f, 0.008f, 0.01f, 0.0125f, 0.016666667f, 0.02f, 0.025f,
    0.033333333f, 0.04f, 0.05f, 0.066666667f, 0.076923077f, 0.1f, 0.125f, 0.166666667f,
    0.2f, 0.25f, 0.333333333f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f, 1.3f, 1.6f, 2.0f, 2.5f,
    3.2f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 13.0f, 15.0f, 20.0f, 25.0f, 30.0f};

static_assert(ARRAY_LENGTH(timeValues) == MaxTimeIndex + 1, "timeValues size must match MaxTimeIndex");

float getTimeByIndex(uint8_t indx)
{
    if (indx > MaxTimeIndex)
    {
        indx = 0;
    }

    return pgm_read_float(&timeValues[indx]);
}

uint8_t findNearestTimeIndex(float t)
{
    float minTime = getTimeByIndex(0);
    if (t <= minTime)
    {
        return 0;
    }

    for (uint8_t i = 0; i < MaxTimeIndex; i++)
    {
        float t1 = getTimeByIndex(i);
        float t2 = getTimeByIndex(i + 1);

        if (t <= t2)
        {
            return (t * t > t1 * t2) ? i + 1 : i;
        }
    }

    return MaxTimeIndex;
}

uint8_t findNearestApertureIndex(float a)
{
    float minAperture = getApertureByIndex(0);
    if (a <= minAperture)
    {
        return 0;
    }

    for (uint8_t i = 0; i < MaxApertureIndex; i++)
    {
        float a1 = getApertureByIndex(i);
        float a2 = getApertureByIndex(i + 1);

        if (a <= a2)
        {
            return (a * a > a1 * a2) ? i + 1 : i;
        }
    }

    return MaxApertureIndex;
}

/*
  Return ND from ndIndex
  int ND[] = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48}; // eg.: 1) 0.3 ND = -1 stop = 2^2 = 4; 2) 0.9 ND = -3 stop = 2^3 = 16;
*/
uint8_t getND(uint8_t ndIndex)
{
    if (ndIndex == 0)
    {
        return 0;
    }

    return 3 + (ndIndex - 1) * 3;
}

// Calculate new exposure value and display it.
void refresh()
{
    ISOMenu = false;
    mainScreen = true;
    NDMenu = false;
    modeMenu = false;
#if LIGHTMETER_DEBUG
    debugMenu = false;
#endif

    float EV = 0;

    float T = getTimeByIndex(T_expIndex);
    float A = getApertureByIndex(apertureIndex);
    long iso = getISOByIndex(ISOIndex);
    float ISOND = iso;

    uint8_t ndStop = getND(ndIndex);

    // if ND filter is configured then make corrections.
    // As ISO is a main operand in all EV calculations we can adjust ISO by ND filter factor.
    // if ND4 (ND 0.6) filter is configured then we need to adjust ISO to -2 full stops. Ex. 800 to 200
    if (ndIndex > 0)
    {
        ISOND = iso / float(1UL << ndIndex);
    }

    if (lux > 0)
    {
        EV = lux2ev(lux);

        if (modeIndex == 0)
        {
            // Aperture priority. Calculating time.
            T_expIndex = findNearestTimeIndex(250 * A * A / ISOND / lux);
            T = getTimeByIndex(T_expIndex); // T = exposure time, in seconds
        }
        else if (modeIndex == 1)
        {
            // Shutter speed priority. Calculating aperture.
            apertureIndex = findNearestApertureIndex(sqrt(lux * ISOND * T / 250));
            A = getApertureByIndex(apertureIndex);
        }
    }
    else
    {
        if (modeIndex == 0)
        {
            T = 0;
        }
        else
        {
            A = 0;
        }
    }

    uint8_t Tdisplay = 0; // Flag for shutter speed display style (fractional, seconds, minutes)
    double Tfr = 0;
    float Tmin = 0;

    if (T >= 60)
    {
        Tdisplay = 0; // Exposure is in minutes
        Tmin = T / 60;
    }
    else if (T < 60 && T >= 0.5)
    {
        Tdisplay = 2; // Exposure in in seconds
    }
    else if (T > 0 && T < 0.5)
    {
        Tdisplay = 1; // Exposure is in fractional form
        Tfr = round(1 / T);
    }
    else
    {
        Tdisplay = 3; // Exposure is out of range
    }

    uint8_t linePos[] = {15, 37};
    display.clearDisplay();
    display.setTextColor(WHITE);

    display.setTextSize(1);
    display.setCursor(13, 1);
    display.print(F("ISO:"));

    if (iso > 999999)
    {
        display.print(iso / 1000000.0, 2);
        display.print(F("M"));
    }
    else if (iso > 9999)
    {
        display.print(iso / 1000.0, 0);
        display.print(F("K"));
    }
    else
    {
        display.print(iso);
    }
    display.drawLine(0, 10, 128, 10, WHITE); // LINE DIVISOR

    display.setCursor(10, linePos[0]);
    display.setTextSize(2);
    display.print(F("f/"));
    if (A > 0)
    {
        if (A >= 100)
        {
            display.print(A, 0);
        }
        else
        {
            display.print(A, 1);
        }
    }
    // else
    // {
    //     outOfrange();
    // }

    display.setTextSize(1);

    drawBatteryIndicator();

    // Metering mode icon
    display.setCursor(0, 1);
    if (meteringMode == 0)
    {
        // Ambient light
        display.print(F("A"));
    }
    else if (meteringMode == 1)
    {
        // Flash light
        display.print(F("F"));
    }
    // End of metering mode icon

    display.setCursor(72, 1);
    display.print(F("lx:"));
    if (SensorError)
    {
        display.print(F("ERR"));
    }
    else if (Overflow)
    {
        display.print(F("OVF"));
    }
    else
    {
        display.print(lux, 0);
    }

    display.drawLine(95, linePos[0] - 1, 95, linePos[0] + 17, WHITE); // LINE DIVISOR
    display.setTextSize(1);
    display.setCursor(100, linePos[0]);
    display.print(F("EV: "));
    display.setCursor(100, linePos[0] + 10);
    if (lux > 0)
    {
        display.println(EV, 0);
    }
    else
    {
        display.println(0, 0);
    }

    // ND filter indicator
    if (ndIndex > 0)
    {
        // display.drawLine(0, 55, 128, 55, WHITE); // LINE DIVISOR
        display.setTextSize(1);
        display.setCursor(0, 57);
        display.print(F("ND"));
        // display.setCursor(100, linePos[0] + 10);
        display.print(1UL << ndIndex);
        display.print(F("="));
        display.println(ndStop / 10.0, 1);
    }

    display.setTextSize(2);
    display.setCursor(10, linePos[1]);
    display.print(F("T:"));

    if (Tdisplay == 0)
    {
        display.print(Tmin, 1);
        display.print(F("m"));
    }
    else if (Tdisplay == 1)
    {
        if (T > 0)
        {
            display.print(F("1/"));
            display.print(Tfr, 0);
        }
        else
        {
            outOfrange();
        }
    }
    else if (Tdisplay == 2)
    {
        display.print(T, 1);
        display.print(F("s"));
    }
    else if (Tdisplay == 3)
    {
        outOfrange();
    }

    // priority marker (shutter or aperture priority indicator)
    display.setTextSize(1);
    display.setCursor(0, linePos[modeIndex] + 5);
    display.print(F("*"));

    drawDebugBuildMarker();
    display.display();

    DEBUG_PRINT(F("r m="));
    DEBUG_PRINT(modeIndex);
    DEBUG_PRINT(F(" mt="));
    DEBUG_PRINT(meteringMode);
    DEBUG_PRINT(F(" lx="));
    DEBUG_PRINT(lux);
    DEBUG_PRINT(F(" ov="));
    DEBUG_PRINT(Overflow);
    DEBUG_PRINT(F(" er="));
    DEBUG_PRINT(SensorError);
    DEBUG_PRINT(F(" ev="));
    DEBUG_PRINT(EV);
    DEBUG_PRINT(F(" iso="));
    DEBUG_PRINT(iso);
    DEBUG_PRINT(F(" f="));
    DEBUG_PRINT(A);
    DEBUG_PRINT(F(" ti="));
    DEBUG_PRINT(T_expIndex);
    DEBUG_PRINT(F(" t="));
    DEBUG_PRINTLN(T);
    DEBUG_FLUSH();

    // display.setTextSize(1);
    // display.setCursor(97, 54);
    // display.print((double)battVolts / 100);
    // display.setCursor(122, 54);
    // display.print(F("V"));

}

void showISOMenu()
{
    ISOMenu = true;
    NDMenu = false;
    mainScreen = false;
    modeMenu = false;
#if LIGHTMETER_DEBUG
    debugMenu = false;
#endif

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(50, 4);
    display.println(F("ISO"));
    display.setTextSize(3);

    long iso = getISOByIndex(ISOIndex);

    if (iso > 999999)
    {
        display.setCursor(0, 40);
    }
    else if (iso > 99999)
    {
        display.setCursor(10, 40);
    }
    else if (iso > 9999)
    {
        display.setCursor(20, 40);
    }
    else if (iso > 999)
    {
        display.setCursor(30, 40);
    }
    else if (iso > 99)
    {
        display.setCursor(40, 40);
    }
    else
    {
        display.setCursor(50, 40);
    }

    display.print(iso);

    drawDebugBuildMarker();
    display.display();
}

void showNDMenu()
{
    ISOMenu = false;
    mainScreen = false;
    NDMenu = true;
    modeMenu = false;
#if LIGHTMETER_DEBUG
    debugMenu = false;
#endif

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 4);
    display.println(F("ND Filter"));
    display.setTextSize(3);

    if (ndIndex > 9)
    {
        display.setCursor(10, 40);
    }
    else if (ndIndex > 6)
    {
        display.setCursor(20, 40);
    }
    else if (ndIndex > 3)
    {
        display.setCursor(30, 40);
    }
    else
    {
        display.setCursor(40, 40);
    }

    if (ndIndex > 0)
    {
        display.print(F("ND"));
        display.print(1UL << ndIndex);
    }
    else
    {
        display.setTextSize(2);
        display.setCursor(10, 40);
        display.print(F("No filter"));
    }

    drawDebugBuildMarker();
    display.display();
}

void showAutoModeMenu()
{
    ISOMenu = false;
    mainScreen = false;
    NDMenu = false;
    modeMenu = true;
#if LIGHTMETER_DEBUG
    debugMenu = false;
#endif

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(14, 4);
    display.println(F("Auto Mode"));
    display.setTextSize(3);

    if (autoModeIndex)
    {
        display.setCursor(48, 40);
    }
    else
    {
        display.setCursor(40, 40);
    }

    if (autoModeIndex)
    {
        display.print(F("ON"));
    }
    else
    {
        display.print(F("OFF"));
    }

    drawDebugBuildMarker();
    display.display();
}

// Navigation menu
void menu()
{
    if (MenuButtonState == 0)
    {
        if (mainScreen)
        {
            autoMode = 0;
            showISOMenu();
        }
        else if (ISOMenu)
        {
            autoMode = 0;
            showNDMenu();
        }
        else if (NDMenu)
        {
            autoMode = 0;
            showAutoModeMenu();
        }
#if LIGHTMETER_DEBUG
        else if (modeMenu)
        {
            autoMode = 0;
            showDebugInfoMenu();
        }
        else if (debugMenu)
        {
            autoMode = autoModeIndex;
            if (autoMode && meteringMode == 0)
            {
                filteredAutoLux = lux;
                autoLuxFilterInitialized = true;
                configureLightMeter(BH1750::CONTINUOUS_HIGH_RES_MODE_2);
                lastAutoModeTime = millis();
            }
            SaveSettings();
            refresh();
        }
#endif
        else
        {
            autoMode = autoModeIndex;
            if (autoMode && meteringMode == 0)
            {
                filteredAutoLux = lux;
                autoLuxFilterInitialized = true;
                configureLightMeter(BH1750::CONTINUOUS_HIGH_RES_MODE_2);
                lastAutoModeTime = millis();
            }
            SaveSettings();
            refresh();
        }
    }

#if LIGHTMETER_DEBUG
    if (debugMenu && (PlusButtonState == 0 || MinusButtonState == 0))
    {
        debugPrintBattery(updateBatteryVoltage(false));
        showDebugInfoMenu();
    }
#endif

    if (NDMenu)
    {
        if (PlusButtonState == 0)
        {
            ndIndex++;

            if (ndIndex > MaxNDIndex)
            {
                ndIndex = 0;
            }
        }
        else if (MinusButtonState == 0)
        {
            if (ndIndex <= 0)
            {
                ndIndex = MaxNDIndex;
            }
            else
            {
                ndIndex--;
            }
        }

        if (PlusButtonState == 0 || MinusButtonState == 0)
        {
            showNDMenu();
        }
    }

    if (modeMenu)
    {
        if (PlusButtonState == 0)
        {
            autoModeIndex = 1 - autoModeIndex;
        }
        else if (MinusButtonState == 0)
        {
            autoModeIndex = 1 - autoModeIndex;
        }

        if (PlusButtonState == 0 || MinusButtonState == 0)
        {
            showAutoModeMenu();
        }
    }

    if (ISOMenu)
    {
        // ISO change mode
        if (PlusButtonState == 0)
        {
            // increase ISO
            ISOIndex++;

            if (ISOIndex > MaxISOIndex)
            {
                ISOIndex = 0;
            }
        }
        else if (MinusButtonState == 0)
        {
            if (ISOIndex > 0)
            {
                ISOIndex--;
            }
            else
            {
                ISOIndex = MaxISOIndex;
            }
        }

        if (PlusButtonState == 0 || MinusButtonState == 0)
        {
            showISOMenu();
        }
    }

    if (ModeButtonState == 0)
    {
        // switching between Aperture priority and Shutter Speed priority.
        if (mainScreen)
        {
            modeIndex++;

            if (modeIndex > 1)
            {
                modeIndex = 0;
            }
        }

        refresh();
    }

    if (mainScreen && MeteringModeButtonState == 0)
    {
        // Switch between Ambient light and Flash light metering
        if (meteringMode == 0)
        {
            meteringMode = 1;
        }
        else
        {
            meteringMode = 0;
            if (autoMode)
            {
                autoLuxFilterInitialized = false;
                configureLightMeter(BH1750::CONTINUOUS_HIGH_RES_MODE_2);
                lastAutoModeTime = millis();
            }
        }

        refresh();
    }

    if (mainScreen && (PlusButtonState == 0 || MinusButtonState == 0))
    {
        if (modeIndex == 0)
        {
            // Aperture priority mode
            if (PlusButtonState == 0)
            {
                // Increase aperture.
                apertureIndex++;

                if (apertureIndex > MaxApertureIndex)
                {
                    apertureIndex = 0;
                }
            }
            else if (MinusButtonState == 0)
            {
                // Decrease aperture
                if (apertureIndex > 0)
                {
                    apertureIndex--;
                }
                else
                {
                    apertureIndex = MaxApertureIndex;
                }
            }
        }
        else if (modeIndex == 1)
        {
            // Time priority mode
            if (PlusButtonState == 0)
            {
                // increase time
                T_expIndex++;

                if (T_expIndex > MaxTimeIndex)
                {
                    T_expIndex = 0;
                }
            }
            else if (MinusButtonState == 0)
            {
                // decrease time
                if (T_expIndex > 0)
                {
                    T_expIndex--;
                }
                else
                {
                    T_expIndex = MaxTimeIndex;
                }
            }
        }

        refresh();
    }
}

/*
  Read buttons state
*/
boolean readButtonPress(uint8_t pin, boolean &previousState, unsigned long &lastPressTime)
{
    boolean currentState = digitalRead(pin);
    boolean pressed = HIGH;
    unsigned long currentTime = millis();

    if (currentState == LOW && previousState == HIGH && currentTime - lastPressTime >= 50)
    {
        pressed = LOW;
        lastPressTime = currentTime;
    }

    previousState = currentState;
    return pressed;
}

void readButtons()
{
    PlusButtonState = readButtonPress(PlusButtonPin, previousPlusButtonState, lastPlusButtonTime);
    MinusButtonState = readButtonPress(MinusButtonPin, previousMinusButtonState, lastMinusButtonTime);
    MeteringButtonState = readButtonPress(MeteringButtonPin, previousMeteringButtonState, lastMeteringButtonTime);
    ModeButtonState = readButtonPress(ModeButtonPin, previousModeButtonState, lastModeButtonTime);
    MenuButtonState = readButtonPress(MenuButtonPin, previousMenuButtonState, lastMenuButtonTime);
    MeteringModeButtonState = readButtonPress(MeteringModeButtonPin, previousMeteringModeButtonState, lastMeteringModeButtonTime);
}
