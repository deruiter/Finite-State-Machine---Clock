/*
 *
 *
 * First attempt to practice Finite State Machine coding
 * 5 feb. 2015 - Erik de Ruiter
 *
 *
 */

//----------------------------------------------------------------------------------------------------------
// Libraries
//----------------------------------------------------------------------------------------------------------
//
#include <Time.h>               // Arduino (new) Time library
                                // -> http://www.pjrc.com/teensy/td_libs_Time.html
#include "DS3232RTC.h"          // supports the Maxim Integrated DS3232 and DS3231 Real-Time Clocks
                                // -> https://github.com/JChristensen/DS3232RTC
#include <Wire.h>               // Enable this line if using Arduino Uno, Mega, etc.
                                //
#include <LedControl.h>         // Maxim 7219 displays library
                                // -> http://playground.arduino.cc/Main/LEDMatrix
#include <SPI.h>                // SPI interface library
                                // -> http://arduino.cc/en/Reference/SPI
#include "ClickButton.h"        // A simple one-button Arduino library to get short and long clicks,
                                // multiple clicks (double click, triple click etc.). 
                                // Click-and-hold is also possible.
                                // -> https://code.google.com/p/clickbutton/

//----------------------------------------------------------------------------------------------------------
// Maxim 7219 Matrix Display initialization
//----------------------------------------------------------------------------------------------------------
// LedConrol(DATAIN, CLOCK, CS/LOAD, NUMBER OF MAXIM CHIPS, CC(false) or CA(true) display)
LedControl lc = LedControl(8, 7, 6, 2, false); // Define pins for Maxim 72xx and how many 72xx we use

//----------------------------------------------------------------------------------------------------------
// Variable and array defenitions
//----------------------------------------------------------------------------------------------------------
// define Maxim 7219 display parameters
#define DISPLAY1                 0           // first Maxim 7219 in 'daisychain' must be '0', next '1' etc.
#define LEDDISPLAYBRIGHTNESS     8           // definition of display brighness level
//DEBUG
#define DEBUGDISPLAY             1           // first Maxim 7219 in 'daisychain' must be '0', next '1' etc.
#define DEBUGDISPLAYBRIGHTNESS   8           // definition of display brighness level

//----------------------------------------------------------------------------------------------------------
// I2C addresses
//----------------------------------------------------------------------------------------------------------
#define CHRONODOT_ADDRESS    0x68       // Chronodot I2C address
#define BH1750_ADDRESS       0x23       // BH1750 Lux Sensor I2C address

//----------------------------------------------------------------------------------------------------------
// define INPUT and OUTPUT pins
//----------------------------------------------------------------------------------------------------------
// INPUT pins:
// pins A4 and A5 are used for the I2C devices
#define BUTTON_UP_PIN           9
#define BUTTON_DOWN_PIN        10
#define BUTTON_SELECT_PIN      11
// OUTPUT pins:
// pins 6, 7 and 8 are used for the Maxim 7221 7-segment display

//----------------------------------------------------------------------------------------------------------
// Buttons definition
//----------------------------------------------------------------------------------------------------------
// create 3 Clickbutton instances, one for each button 
ClickButton buttonUp(BUTTON_UP_PIN, LOW, CLICKBTN_PULLUP);
ClickButton buttonDown(BUTTON_DOWN_PIN, LOW, CLICKBTN_PULLUP);
ClickButton buttonSelect(BUTTON_SELECT_PIN, LOW, CLICKBTN_PULLUP);
// user input actions
enum {BTN_SELECT_SHORT, BTN_SELECT_LONG, BTN_UP, BTN_DOWN};

//----------------------------------------------------------------------------------------------------------
// Related to the State Machine
//----------------------------------------------------------------------------------------------------------
// The different states of the system
enum states
{
    SHOW_TIME,      // Displays the time. When SELECT is pressed, it displays the date
                    //   If SELECT is pressed for more than 1 second, it moves to SET_HOUR etc.
    SHOW_DATE,      // Displays the date. When SELECT is pressed, it returns to displaying the time
                    //   Otherwise, it times out after 5 seconds and returns to displaying the time
    SET_HOUR,       // Option for setting the hours. If provided, it moves on to minutes.
                    //   Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
    SET_MINUTES,    // Option for setting the minutes. If provided, it moves on to day
                    //   Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
    SET_DAY,        // Option for setting the day. If provided, it moves on to month
                    //   Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
    SET_MONTH,      // Option for setting the month. If provided, it moves on to year
                    //   Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
    SET_YEAR,       // Option for setting the year. If provided, it finally sets the year and updates the RTC
                    //   Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
};
states state;       // Holds the current state of the system

//----------------------------------------------------------------------------------------------------------
// miscelleanous variables
//----------------------------------------------------------------------------------------------------------
int8_t button;                              // holds the current button pressed
int8_t trigger;                             // holds the last user action (button hold/pressed)

int8_t TMPsecond;                           // holds time & date while setting the time
int8_t TMPminute;
int8_t TMPhour;    
int8_t TMPweekday; 
int8_t TMPday;    
int8_t TMPmonth;   
int8_t TMPyear;    

uint32_t currentTime  = 0;                  // used in blink display function
uint32_t previousTime = 0;                  // used in blink display function
boolean blinkDisplay  = false;              // used in blink display function
boolean RTCERROR = true;                    // flag to check RTC communication, used in SHOW_TIME and SHOW_DATE
byte buff[2];                               // assign BH1750 Lux Sensor data buffer


//==========================================================================================================
// SETUP
//==========================================================================================================

void setup()
{
    Wire.begin();                           // start I2C
    Serial.begin(9600);                     // start Serial for debug purposes
    BH1750_Init();                          // start BH1750 Lux Sensor
    setSyncProvider(RTC.get);               // set RTC as the Syncprovider
    setSyncInterval(5);                     // time in sec of resync with RTC
    initializeDisplay();                    // initialize 7 segment display
 
    state = SHOW_TIME;                      // Initial state of the FSM
}


//==========================================================================================================
// LOOP
//==========================================================================================================

void loop()
{
    executeState();                         // execute (new) state of FSM
    checkUserInput();                       // check for button state/user input
    checkRTCstatus();                       // check connection with RTC, ERROR: activate decimal point 
    adjustBrightness();                     // check ambient light and adjust the display brightness
}


//================================================================================================================
//
// Finite State Machine
//
//================================================================================================================

void executeState()
{
    //DEBUG - show STATE
    lc.setChar(DEBUGDISPLAY, 7, state, true);

    // Uses the current state to decide what to process
    switch (state)
    {
        case SHOW_TIME:
            displayCurrentTime();
            displayCurrentDay();
            break;
        case SHOW_DATE:
            displayCurrentDate();
            displayCurrentDay();
            break;
        case SET_HOUR:
            displayTemporaryTime();
            break;
        case SET_MINUTES:
            displayTemporaryTime();
            break;
        case SET_DAY:
            displayTemporaryDate();
            break;
        case SET_MONTH:
            displayTemporaryDate();
            break;
        case SET_YEAR:
            displayTemporaryDate();
            break;
        break;
    }

}

//----------------------------------------------------------------------------------------------------------
// check for user input
void checkUserInput()
{   
    buttonUp.Update();                      // check state of buttons
    buttonDown.Update();                    // 1 = short click, -1 = long click (>1 second)
    buttonSelect.Update();                  // 0 = no input, Duh!

    switch (buttonSelect.clicks)            // check for input of SELECT button
    {
        case 1:
            button = BTN_SELECT_SHORT;      // SELECT button is shortly pressed
            transition(button);
        break;
        case -1:                            // SELECT button is pressed for > 1 second
            button = BTN_SELECT_LONG;
            transition(button);
        break;
    }
    switch (buttonUp.clicks)                // check for user action of UP BUTTON
    {
        case 1:
            button = BTN_UP;
            transition(button);
        break;
    }
    switch (buttonDown.clicks)              // check for user action of DOWN BUTTON
    {
        case 1:
            button = BTN_DOWN;
            transition(button);
        break;
    }
}

//----------------------------------------------------------------------------------------------------------
// change state of the "state machine" based on the user input;
// also perform one-time actions required by the change of state (e.g. clear display);
void transition(int trigger)
{
    //DEBUG - show pressed BUTTON
    lc.setChar(1, 6, trigger, true);

    // Uses the current state to decide what to process
    switch (state)
    {
        case SHOW_TIME:
            if (trigger == BTN_SELECT_SHORT)
            {
                clearDisplay();
                state = SHOW_DATE;
            }
            else if(trigger == BTN_SELECT_LONG)
            {
                clearDisplay();
                timeDateToTemporaryMem();
                state = SET_HOUR;
            }
            break;
        case SHOW_DATE:
            if (trigger == BTN_SELECT_SHORT)
            {
                clearDisplay();
                state = SHOW_TIME;
            }
            break;
        case SET_HOUR:
            if (trigger == BTN_UP)
            {
                TMPhour++; 
                if (TMPhour>23) TMPhour = 0;
            }
            else if (trigger == BTN_DOWN)
            {
                TMPhour--; 
                if (TMPhour<0) TMPhour = 23;
            }
            if (trigger == BTN_SELECT_SHORT)
            {
                state = SET_MINUTES;            
            }
            break;
        case SET_MINUTES:
            if (trigger == BTN_UP)
            {
                TMPminute++; 
                if (TMPminute>59) TMPminute = 0;
            }
            else if (trigger == BTN_DOWN)
            {
                TMPminute--; 
                if (TMPminute<0) TMPminute = 59;
            }
            // short press SELECT BUTTON = continue with SET_DAY
            if (trigger == BTN_SELECT_SHORT)
            {
                state = SET_DAY;                
            }
            // long press SELECT BUTTON = exit time set to SHOW_TIME
            if (trigger == BTN_SELECT_LONG)
            {
                writeRTC();
                setTime(TMPhour, TMPminute, TMPsecond, day(), month(), year());
                clearDisplay();
                state = SHOW_TIME;              
            }
            break;
        case SET_DAY:
            if (trigger == BTN_UP)
            {
                TMPday++; 
                if (TMPday>31) TMPday = 1;
            }
            else if (trigger == BTN_DOWN)
            {
                TMPday--; 
                if (TMPday<1) TMPday = 31;
            }
            if (trigger == BTN_SELECT_SHORT)
            {
                state = SET_MONTH;              
            }
            break;
        case SET_MONTH:
            if (trigger == BTN_UP)
            {
                TMPmonth++; 
                if (TMPmonth>12) TMPmonth = 1;
            }
            else if (trigger == BTN_DOWN)
            {
                TMPmonth--; 
                if (TMPmonth<1) TMPmonth = 12;
            }
            if (trigger == BTN_SELECT_SHORT)
            {
                state = SET_YEAR;               
            }
            break;
        case SET_YEAR:
            if (trigger == BTN_UP)
            {
                TMPyear++; 
                if (TMPyear>99) TMPyear = 0;
            }
            else if (trigger == BTN_DOWN)
            {
                TMPyear--; 
                if (TMPyear<0) TMPyear = 99;
            }
            if (trigger == BTN_SELECT_SHORT)
            {
                writeRTC();
                setTime(TMPhour, TMPminute, TMPsecond, TMPday, TMPmonth, TMPyear);
                clearDisplay();
                state = SHOW_TIME;              
            }
            break;
    }
}


//================================================================================================================
//
// Functions
//
//================================================================================================================

//----------------------------------------------------------------------------------------------------------
void displayCurrentTime()
{
    lc.setChar(DISPLAY1, 5, ((hour() < 10) ? ' ' : (hour() / 10)), false);
    lc.setChar(DISPLAY1, 4, (hour() % 10), false);
    lc.setChar(DISPLAY1, 3, (minute() / 10), false);
    lc.setChar(DISPLAY1, 2, (minute() % 10), false);
    lc.setChar(DISPLAY1, 1, (second() / 10), false);
    lc.setChar(DISPLAY1, 0, (second() % 10), RTCERROR);    // decimal point is ON/OFF depending on status RTCERROR
}

//----------------------------------------------------------------------------------------------------------
void displayCurrentDate()
{
 // display Date - with dashes between D-M-Y
    lc.setChar(DISPLAY1, 5, day() / 10, false);
    lc.setChar(DISPLAY1, 4, day() % 10, false);
    lc.setChar(DISPLAY1, 3, month() / 10, false);
    lc.setChar(DISPLAY1, 2, month() % 10, false);
    lc.setChar(DISPLAY1, 1, (year()-2000) / 10, false);
    lc.setChar(DISPLAY1, 0, (year()-2000) % 10, RTCERROR); // decimal point is ON/OFF depending on status RTCERROR
                                                    // see checkRTCstatus() function
}
//----------------------------------------------------------------------------------------------------------
void displayCurrentDay()
{
    // not yet implemented    

 
}

//----------------------------------------------------------------------------------------------------------
// copy current time/date into temporary variables while adjusting
void timeDateToTemporaryMem()
{
    TMPsecond  = 0;                         // set seconds to zero
    TMPminute  = minute();
    TMPhour    = hour();    
    TMPweekday = weekday(); 
    TMPday     = day();    
    TMPmonth   = month();   
    TMPyear    = (year() - 2000);           // we don't need the 'century part', only last 2 digits
}

//----------------------------------------------------------------------------------------------------------
// during adjustment of the time/date, temporary variables are used (seconds are set to zero)
// Ternary operator is used to blink the display which is adjusted
// ( CONDITION ? Execute when TRUE : Execute when FALSE )
void displayTemporaryTime()
{
    switch (state)
    {
    case SET_HOUR:
        lc.setChar(DISPLAY1, 7, (blinkDisplay == true) ? (TMPhour / 10) : ' ', false);     // Adjust HOUR
        lc.setChar(DISPLAY1, 6, (blinkDisplay == true) ? (TMPhour % 10) : ' ', false);
        lc.setChar(DISPLAY1, 5, (TMPminute / 10), false);
        lc.setChar(DISPLAY1, 4, (TMPminute % 10), false);
    break;
    case SET_MINUTES:
        lc.setChar(DISPLAY1, 7, (TMPhour / 10), false);
        lc.setChar(DISPLAY1, 6, (TMPhour % 10), false);
        lc.setChar(DISPLAY1, 5, (blinkDisplay == true) ? (TMPminute / 10) : ' ', false);   // Adjust MINUTES
        lc.setChar(DISPLAY1, 4, (blinkDisplay == true) ? (TMPminute % 10) : ' ', false);
    break;
    }
    
    // Blink routine
    currentTime = millis();
    if (currentTime - previousTime >= 500)
    {
        // reverse blinkDisplay state
        blinkDisplay == true ? blinkDisplay = false : blinkDisplay = true;
        // reset previousTime
        previousTime = millis();
    }
}

//----------------------------------------------------------------------------------------------------------
// during adjustment of the date, temporary variables are used
// Ternary operator is used to blink the display which is adjusted
// ( CONDITION ? Execute when TRUE : Execute when FALSE )
void displayTemporaryDate()
{
    switch (state)
    {
    case SET_DAY:
        lc.setChar(DISPLAY1, 7, (blinkDisplay == true) ? (TMPday / 10) : ' ', false);      // Adjust DAY
        lc.setChar(DISPLAY1, 6, (blinkDisplay == true) ? (TMPday % 10) : ' ', false);      
        lc.setChar(DISPLAY1, 5, '-', false);
        lc.setChar(DISPLAY1, 4, (TMPmonth / 10), false);
        lc.setChar(DISPLAY1, 3, (TMPmonth % 10), false);
        lc.setChar(DISPLAY1, 2, '-', false);
        lc.setChar(DISPLAY1, 1, (TMPyear / 10), false);
        lc.setChar(DISPLAY1, 0, (TMPyear % 10), false);
    break;
    case SET_MONTH:
        lc.setChar(DISPLAY1, 7, (TMPday / 10), false);
        lc.setChar(DISPLAY1, 6, (TMPday % 10), false);
        lc.setChar(DISPLAY1, 5, '-', false);
        lc.setChar(DISPLAY1, 4, (blinkDisplay == true) ? (TMPmonth / 10) : ' ', false);    // Adjust MONTH
        lc.setChar(DISPLAY1, 3, (blinkDisplay == true) ? (TMPmonth % 10) : ' ', false);    
        lc.setChar(DISPLAY1, 2, '-', false);
        lc.setChar(DISPLAY1, 1, (TMPyear / 10), false);
        lc.setChar(DISPLAY1, 0, (TMPyear % 10), false);
    break;
    case SET_YEAR:
        lc.setChar(DISPLAY1, 7, (TMPday / 10), false);
        lc.setChar(DISPLAY1, 6, (TMPday % 10), false);
        lc.setChar(DISPLAY1, 5, '-', false);
        lc.setChar(DISPLAY1, 4, (TMPmonth / 10), false);
        lc.setChar(DISPLAY1, 3, (TMPmonth % 10), false);
        lc.setChar(DISPLAY1, 2, '-', false);
        lc.setChar(DISPLAY1, 1, (blinkDisplay == true) ? (TMPyear / 10) : ' ', false);     // Adjust YEAR
        lc.setChar(DISPLAY1, 0, (blinkDisplay == true) ? (TMPyear % 10) : ' ', false);     
    break;
    }

    // Blink routine
    currentTime = millis();
    if (currentTime - previousTime >= 500)
    {
        // reverse blinkDisplay state
        blinkDisplay == true ? blinkDisplay = false : blinkDisplay = true;
        // reset previousTime
        previousTime = millis();
    }
}
//----------------------------------------------------------------------------------------------------------
// write temporary time/date information to the RTC
void writeRTC()
{
    Wire.beginTransmission(CHRONODOT_ADDRESS);
    Wire.write(0x00);                       //stop Oscillator
    Wire.write(decToBcd(TMPsecond));
    Wire.write(decToBcd(TMPminute));
    Wire.write(decToBcd(TMPhour));
    Wire.write(decToBcd(TMPweekday));
    Wire.write(decToBcd(TMPday));
    Wire.write(decToBcd(TMPmonth));
    Wire.write(decToBcd(TMPyear));
    Wire.write(0x00);                       //start 
    Wire.endTransmission();
}
//----------------------------------------------------------------------------------------------------------
// clears the 7 segemnt display
void clearDisplay()
{
    lc.clearDisplay(0);
}
//----------------------------------------------------------------------------------------------------------
// initialize 7 sgement display and set brightness level
void initializeDisplay()
{
    lc.shutdown(DISPLAY1, false);                           // Maxim 7219 display display wake up
    lc.clearDisplay(DISPLAY1);                              // clear display
    lc.setIntensity(DISPLAY1, LEDDISPLAYBRIGHTNESS);        // set brightness level
    //DEBUG
    lc.shutdown(DEBUGDISPLAY, false);                       // Maxim 7219 display display wake up
    lc.clearDisplay(DEBUGDISPLAY);                          // clear display
    lc.setIntensity(DEBUGDISPLAY, DEBUGDISPLAYBRIGHTNESS);  // set brightness level
}
//----------------------------------------------------------------------------------------------------------
// check communication with RTC and activate india=cator when there are problems
void checkRTCstatus()
{
    if (timeStatus() != timeSet) 
         RTCERROR = true;                   // RTC could not be read: activate decimal point on display 
    else
         RTCERROR = false;                  // RTC could be read: deactivate decimal point on display 
}
//----------------------------------------------------------------------------------------------------------
// check ambient light and adjust the display brightness
void adjustBrightness()
{
    byte i=0;
    Wire.beginTransmission(BH1750_ADDRESS);
    Wire.requestFrom(BH1750_ADDRESS, 2);
    while(Wire.available())
    {
        buff[i] = Wire.read(); 
        i++;
    }
    Wire.endTransmission();  

    float valf=0;
    if(i == 2)
    {
        valf = ( (buff[0]<<8) | buff[1] ) / 1.2;
        lc.setIntensity(0, map(valf, 0, 300, 0, 15));  // set brightness level
    }
}
//----------------------------------------------------------------------------------------------------------
// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val)
{
    return ( (val/10*16) + (val%10) );
}
//----------------------------------------------------------------------------------------------------------
// Convert binary coded decimal to decimal
byte bcdToDec(byte val)
{
    return ( (val/16*10) + (val%16) );
}
//----------------------------------------------------------------------------------------------------------
// BH1750 Lux Sensor initialisation 
void BH1750_Init()
{
    Wire.beginTransmission(BH1750_ADDRESS);
    Wire.write(0x10);                       // 1 [lux] resolution
    Wire.endTransmission();
}
//----------------------------------------------------------------------------------------------------------
// REFERENCE SECTION
/*

Ledcontrol library:

lc.clearDisplay(int addr) ..................................... clears the selected display
lc.shutdown(int addr, boolean) ................................ wake up the MAX72XX from power-saving mode (true = sleep, false = awake)
lc.setIntensity(int addr, value) .............................. set a medium brightness for the Leds (0=min - 15=max)
lc.setLed(int addr, int row, int col, boolean state) .......... switch on the led in row, column. remember that indices start at 0!
lc.setRow(int addr, int row, byte value) ...................... this function takes 3 arguments. example: lc.setRow(0,2,B10110000);
lc.setColumn(int addr, int col, byte value) ................... this function takes 3 arguments. example: lc.setColumn(0,5,B00001111);
lc.setDigit(int addr, int digit, byte value, boolean dp) ....... this function takes an argument of type byte and prints the corresponding digit on the specified column.
The range of valid values runs from 0..15. All values between 0..9 are printed as digits,
values between 10..15 are printed as their hexadecimal equivalent
lc.setChar(int addr, int digit, char value, boolean dp) ....... will display: 0 1 2 3 4 5 6 7 8 9 A B C D E F H L P; - . , _ <SPACE> (the blank or space char)

pin 12/7 is connected to the DataIn
pin 11/6 is connected to the CLK
pin 10/5 is connected to CS/LOAD

***** Please set the number of devices you have *****
But the maximum default of 8 MAX72XX wil also work.
LedConrol(DATAIN, CLOCK, CS/LOAD, NUMBER OF MAXIM CHIPS)


*/


