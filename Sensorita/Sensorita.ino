// -*-c++-*-

/*
 * Collect sensor data, display on LCD and
 * send via HW-Serial to Leonardo (see EtherLog.ino),
 * which then uploads it to Django.
 *
 * Target platform: Arduino Micro
 */

#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>
#include <EmonLib.h>
#include "DHT.h"
#include "Sensors.h"

#include <avr/wdt.h>

// how long are we running
long uptime = 0;
const int MAXUP = 86400;

// temp+humidity sensor
#define DHTPIN 11
#define DHTTYPE DHT22 
DHT dht(DHTPIN, DHTTYPE);


// current sensor
EnergyMonitor emon;
const int CurrentPin = 1; // analog pin1


// DS1820 Dallas Temp Seonsors
// Specify the same count on all 3 variables
int TempPins = 5;
DeviceAddress DallasAddresses[4]; 


// Dallas Onewire patched to:
#define ONE_WIRE_BUS 10
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature dallas(&oneWire);


// our sensor storage
Sensor SensorsT[5]; // DS1820 + DHT22 Temp sensor
Sensor SensorL;     // LDR light sensor
Sensor SensorH;     // DHT22 humidity sensor
EmonSensor SensorP;  // SCT13 power sensor

// LCD Pins, patched to:
//                RS, E, D4, D5, D6, D7
LiquidCrystal lcd(7,  8, 9,  4,  5,  6);

// LCD backlight transistor patched to:
const int LCDBacklight = 13;

// custom ° char
byte deg[8] = { 0x2,0x5,0x2,0x0,0x0,0x0,0x0 };

// LDR patch and vars
const int photocellPin = 0;
int photocellReading;
float ref = 1.0;
float vout;

// control button patched to:
const int ControlButton = 2;
const int ResetButton   = 3;

// arduino micro pin => interrupt mappings:
// interrupt   |  pin
//         0   |  3
//         1   |  2
//         2   |  0
//         3   |  1
const int ControlInterrupt = 1;
const int ResetInterrupt   = 0;


// timer constants (seconds)
const unsigned long BacklightTimeout = 60000; // 1 minute = 60 * 1000
volatile unsigned long LCDTimer     = 0;
volatile unsigned long LCDMoment    = 0; 
bool BacklightOn = false;

// menu mode: 0 = main, 1..n = sensor details
int MenuMode, PreviousMode          = 0;

// various menu modi we support
int MenuMain, MenuLightsOn, MenuLux, MenuHumidity, MenuCurrent = 0;

// when to read sensors, milliseconds
const unsigned long SensorReadIntervall = 5000; // every 5 seconds
const unsigned long LogIntervall        = 60000; // every 1 minute
unsigned long SensorTimer     = 0;
unsigned long SensorMoment    = 0;
unsigned long LogTimer        = 0;
unsigned long LogMoment       = 0;

// WDT state
unsigned long WdtState = 0;


void reboot () {
  asm volatile ("  jmp 0");
}

void control_pressed() {
 // invoked on key press interrupt
 if(MenuMode == MenuCurrent) {
   if(BacklightOn) {
     MenuMode = MenuLightsOn;
   }
   else {
     MenuMode = 0;
   }
 }
 else {
   MenuMode++;
 }
 
 // and reset backlight timeout
 LCDTimer = LCDMoment;
}




void setup()   {                
  Serial1.begin(9600);
  while (!Serial1) ;

  // connect the lcd
  lcd.createChar(0, deg);
  lcd.begin(16, 2);
  delay(1000);

  // initialize dallas temperature sensors
  dallas.begin();

  //initialize the dht22
  dht.begin();

  // how many of them do we have?
  TempPins = dallas.getDeviceCount() + 1;

  // initialize sensor structs
  // DHT = 0
  for(int id=0; id<TempPins+1; id++) {
    SensorsT[id].current = 0;
    SensorsT[id].min     = 0;
    SensorsT[id].max     = 0;
  }
  SensorL.current = 0;
  SensorL.min     = 0;
  SensorL.max     = 0;

  SensorH.current = 0;
  SensorH.min     = 0;
  SensorH.max     = 0;

  SensorP.AmpereCurrent = 0;
  SensorP.AmpereMin     = 0;
  SensorP.AmpereMax     = 0;
  SensorP.WattsCurrent  = 0;
  SensorP.WattsMin      = 0;
  SensorP.WattsMax      = 0;

  // calculate menu poins
  MenuLightsOn = 1;
  MenuHumidity = TempPins     + 2; // lightson + dht
  MenuLux      = MenuHumidity + 1;
  MenuCurrent  = MenuLux      + 1;

  // various pins
  pinMode(ControlButton, INPUT);
  pinMode(ResetButton, INPUT);
  pinMode(LCDBacklight, OUTPUT);

  // use interrupts for user buttons
  attachInterrupt(ControlInterrupt, control_pressed, RISING);
  attachInterrupt(ResetInterrupt, reboot,   RISING);

  // initialize current sensor
  emon.current(CurrentPin, 111.1); 

  // enable 2 seconds watchdog timer.
  // takes some time to collect everything due to sampling etc
  wdt_enable(WDTO_2S);
}



void get_dallas () {
  // fetch all dallas DS1820 temperature readings
  dallas.requestTemperatures();
  for (int id=1; id < TempPins; id++) {
    SensorsT[id].current = dallas.getTempCByIndex(id - 1); // dallas starts count from 0
    if(SensorsT[id].current < 70) {
      minmax_f(&SensorsT[id]);
    }
  }
}

void get_lux () {
  // fetch photocell reading and calculate lux
  photocellReading = analogRead(photocellPin);
  vout             = 0.0048828125 * photocellReading;
  SensorL.current  = 5000.0 / (ref * ((5.0 - vout) / vout));
  minmax_f(&SensorL);
}

void get_ampere () {
  // get SCT13 reading and calculate ampere and watts
  SensorP.AmpereCurrent = emon.calcIrms(1480) / 100;
  minmax_d(&SensorP);
}

void get_dht () {
  // get DHT22 temperature and humidity reading
  float t = dht.readTemperature();
  if (isnan(t)) {
    SensorsT[0].current = 0;
  }
  else {
    SensorsT[0].current = t;
  }
  float h = dht.readHumidity();
  if (isnan(h)) {
    SensorH.current = 0;
  }
  else {
    SensorH.current = h;
  }
  minmax_f(&SensorsT[0]);
  minmax_f(&SensorH);
}


void get_sensors () {
  // fetch all sensor readings
  get_dallas();
  get_lux();
  get_dht();
  get_ampere();
}

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) lcd.print("0");
    lcd.print(deviceAddress[i], HEX);
  }
}

void log_sensors() {
  // log sensor values to TTL serial out
  for (int id=0; id<TempPins; id++) {
    Serial1.print(SensorsT[id].current);
    Serial1.print('/');
  }

  Serial1.print(SensorH.current);
  Serial1.print('/');

  Serial1.print(SensorL.current);
  Serial1.print('/');

  Serial1.print(SensorP.AmpereCurrent);
  Serial1.print('/');

  Serial1.print(SensorP.WattsCurrent);
  Serial1.print('/');

  Serial1.print(uptime);
  Serial1.println('/');
}

// Watchdog algorithm via
// http://www.ganssle.com/item/great-watchdog-timers.htm
void halt() {
 while(1);
}

void wdt_a() {
  if (WdtState != 0x5555) {
    // halt the watchdog, which leads to its timeout
    // and finally the system reset
    halt();
  }

  // works, unless the sun erupts
  WdtState += 0x1111;

  if (WdtState != 0x6666) {
    // what the what?!
    halt();
  }
}

void wdt_b() {
  if (WdtState != 0x8888) {
    // state calculations failed so we assume system failure.
    // halt the watchdog, which leads to its timeout
    // and finally the system reset
    halt();
  }
  else {
    // kick the dog
    wdt_reset();
  }

  WdtState = 0;

  if (WdtState != 0) {
    // what the what?!
    halt();
  }
}


void screen () {
  // print the lcd display. what will be displayed depends on
  // the value of MenuMode
  if (MenuMode == MenuMain || MenuMode == MenuLightsOn) {
    // main, display temp average and lux
    // FIXME: show rounded integers of all sensors here
    float SumTemp = 0;
    for (int id=0; id<TempPins; id++) {
      SumTemp += SensorsT[id].current;
    }
    SumTemp = SumTemp / TempPins; // average
    
    lcd.setCursor(0, 0);
    lcd.print(SumTemp);
    lcd.write((uint8_t)0);
    lcd.print("C  ");

    lcd.print(SensorH.current);
    lcd.print(" %");

    lcd.setCursor(0, 1);
    lcd.print((int)SensorL.current);
    lcd.print(" Lux  ");
    
    lcd.print(int(SensorP.WattsCurrent));
    lcd.print(" W   ");
  }
  else if (MenuMode == MenuLux) {
    // display lux sensors
    lcd.setCursor(0, 0);
    lcd.print("Lux: ");
    lcd.print(SensorL.current);
    lcd.print("            ");
    
    lcd.setCursor(0, 1);
    lcd.print("- ");
    lcd.print((int)SensorL.min);
    lcd.print(" + ");
    lcd.print((int)SensorL.max);
    lcd.print("       ");
  }
  else if (MenuMode > MenuLightsOn && MenuMode < MenuHumidity) {
    // one temp sensor per screen
    int id = MenuMode - 2;
    lcd.setCursor(0, 0);
    if(id == 0) {
      lcd.print("DHT");
    }
    else {
      lcd.print("DS");
      lcd.print(id);
    }
    lcd.print(" ");
    lcd.write((uint8_t)0);
    lcd.print("C ");

    lcd.print(SensorsT[id].current);
    lcd.print(" ");

    lcd.setCursor(0, 1);
    lcd.print("- ");
    lcd.print(SensorsT[id].min);
    lcd.print(" + ");
    lcd.print(SensorsT[id].max);
    lcd.print("   ");

    /*
    DeviceAddress tempDeviceAddress;
    if(dallas.getAddress(tempDeviceAddress, id)) {
      lcd.setCursor(0,1);
      printAddress(tempDeviceAddress);
    }
    */
  }
  else if(MenuMode == MenuHumidity) {
    // DHT Humidity display
    lcd.setCursor(0, 0);
    lcd.print("Humidity: ");
    lcd.print(SensorH.current);
    lcd.print("%    ");
    
    lcd.setCursor(0, 1);
    lcd.print("- ");
    lcd.print(SensorH.min);
    lcd.print(" + ");
    lcd.print(SensorH.max);
    lcd.print("     ");
  }
  else if(MenuMode == MenuCurrent) {
    // Watt + Ampere display
    lcd.setCursor(0, 0);
    lcd.print(SensorP.AmpereCurrent);
    lcd.print(" A  ");
    lcd.print(SensorP.WattsCurrent);
    lcd.print(" W    ");
    
    lcd.setCursor(0, 1);
    lcd.print("Max: ");
    lcd.print((int)SensorP.AmpereMax);
    lcd.print("A ");
    lcd.print((int)SensorP.WattsMax);
    lcd.print("W ");
  }
}



void loop(void) {
  // reset WDT state
  WdtState = 0x5555;
  wdt_a();

  // record current moments
  LCDMoment = SensorMoment = LogMoment = millis();

  // check if there is enough time gone, to re-check the sensors
  if (SensorMoment - SensorTimer > SensorReadIntervall) {
    get_sensors();
    SensorTimer = SensorMoment;
  }

  // the user pressed the control button, keep new state and clear screen
  if(MenuMode != PreviousMode) {
    lcd.clear();
    PreviousMode = MenuMode; 
    digitalWrite(LCDBacklight, HIGH);
    BacklightOn = true;
    LCDTimer = LCDMoment;
  }

  // user action timeout (did not press any key after BackLightTimeout milliseconds
  // turn off backlight and fall back to main menu
  if(LCDMoment - LCDTimer > BacklightTimeout) {
    digitalWrite(LCDBacklight, LOW);
    BacklightOn = false;
    MenuMode = 0;
    if(MenuMode != PreviousMode) {
      lcd.clear();
    }
    PreviousMode = 0;
  }
  
  // log sensor values to serial1
  if (LogMoment - LogTimer > LogIntervall) {
    log_sensors();
    LogTimer = LogMoment;
  }

  // finally, re-paint the screen
  screen();
  
  // for debugging, put current menu
  //lcd.setCursor(15, 1);
  //lcd.print(MenuMode);

  if(uptime < MAXUP) {
    // log uptime in seconds
    uptime = millis() / 1000;
  }

  // hopefully reset WDT
  WdtState += 0x2222;
  wdt_b();
}
