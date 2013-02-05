#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal.h>
#include <EmonLib.h>
#include "DHT.h"

#include <avr/wdt.h>

// temp+humidity sensor
#define DHTPIN 11
#define DHTTYPE DHT22 
DHT dht(DHTPIN, DHTTYPE);
float DHT_T;
float DHT_H;
const char* DHT_Name = "VO RECHTS";

// current sensor
EnergyMonitor emon;
const int CurrentPin = 1; // analog pin1
double EnergyAmpere  = 0;
double EnergyWatts   = 0;
long EnergyVcc       = 0;

// DS1820 Dallas Temp Seonsors
// Specify the same count on all 3 variables
int TempPins = 4;
DeviceAddress DallasAddresses[4]; 
float CurrentTemps[4];


// Dallas Onewire patched to:
#define ONE_WIRE_BUS 10
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
const char* TempNames[] = { "LI UNTEN", "HINT LI OBEN", "HINT RE UNTEN", "RE OBEN" };

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
int lux;
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
int MenuMain, MenuLightsOn, MenuLux, MenuHumidity, MenuCurrent, MenuDHT = 0;

// when to read sensors, milliseconds
const unsigned long SensorReadIntervall = 5000; // every 5 seconds
unsigned long SensorTimer     = 0;
unsigned long SensorMoment    = 0;






void setup()   {                
  Serial.begin(9600);

  // connect the lcs
  lcd.createChar(0, deg);
  lcd.begin(16, 2);
  delay(1000);  

  // initialize dallas temperature sensors
  sensors.begin();

  //initialize the dht22
  dht.begin();

  // how many of them do we have?
  TempPins = sensors.getDeviceCount();

  // calculate menu poins
  MenuLightsOn = 1;
  MenuDHT      = TempPins + 2;
  MenuLux      = MenuDHT + 1;
  MenuHumidity = MenuLux + 1;
  MenuCurrent  = MenuHumidity + 1;

  // various pins
  pinMode(ControlButton, INPUT);
  pinMode(ResetButton, INPUT);
  pinMode(LCDBacklight, OUTPUT);

  // use interrupts for user buttons
  attachInterrupt(ControlInterrupt, control_pressed, RISING);
  attachInterrupt(ResetInterrupt, reboot,   RISING);

  // initialize current sensor
  emon.current(CurrentPin, 111.1); 

  // fetch sensor values for the 1st time
  get_sensors();

  // enable 8 seconds watchdog timer
  wdt_enable(WDTO_8S);
}


void get_sensors () {
  // fetch all sensor readings
  get_dallas();
  get_lux();
  get_dht();
  get_current();
}


void get_dallas () {
  // fetch all dallas DS1820 temperature readings
  sensors.requestTemperatures();
  for (int id=0; id<TempPins; id++) {
    CurrentTemps[id] = sensors.getTempCByIndex(id);
  }
}

void get_lux () {
  // fetch photocell reading and calculate lux
  photocellReading = analogRead(photocellPin);
  vout = 0.0048828125 * photocellReading;
  lux  = 500 / (ref * ((5 - vout) / vout));
}

void get_current () {
  // get SCT13 reading and calculate ampere and watts
  EnergyAmpere = emon.calcIrms(1480) / 100;
  EnergyWatts  = EnergyAmpere * 230.0;
}

void get_dht () {
  // get DHT22 temperature and humidity reading
  float t = dht.readTemperature();
  if (isnan(t)) {
    DHT_T = 0;
  }
  else {
    DHT_T = t;
  }
  float h = dht.readHumidity();
  if (isnan(h)) {
    DHT_H = 0;
  }
  else {
    DHT_H  = h;
  }
}

void screen () {
  // print the lcd display. what will be displayed depends on
  // the value of MenuMode
  if (MenuMode == MenuMain || MenuMode == MenuLightsOn) {
    // main, display temp average and lux
    // FIXME: show rounded integers of all sensors here
    float SumTemp = DHT_T;
    for (int id=0; id<TempPins; id++) {
      SumTemp += CurrentTemps[id];
    }
    SumTemp = SumTemp / (TempPins + 1); // average
    
    lcd.setCursor(0, 0);
    lcd.print(SumTemp);
    lcd.write((uint8_t)0);
    lcd.print("C  ");

    lcd.print(DHT_H);
    lcd.print("%");

    lcd.setCursor(0, 1);
    lcd.print(lux);
    lcd.print(" Lux  ");
    
    lcd.print(int(EnergyWatts));
    lcd.print("W");
  }
  else if (MenuMode == MenuLux) {
    // display lux sensors
    lcd.setCursor(0, 0);
    lcd.print("Light: ");
    lcd.setCursor(0,1);
    lcd.print(lux);
    lcd.print(" Lux  ");
  }
  else if (MenuMode > MenuLightsOn && MenuMode < MenuDHT) {
    // one temp sensor per screen
    int id = MenuMode - 2;
    lcd.setCursor(0, 0);
    lcd.print(TempNames[id]);
    lcd.setCursor(0, 1);
    lcd.print(" Temp: ");
    lcd.print(CurrentTemps[id]);
    lcd.print(" ");
    lcd.write((uint8_t)0);
    lcd.print("C");
  }
  else if(MenuMode == MenuDHT) {
    // DHT temp display
    lcd.setCursor(0, 0);
    lcd.print(DHT_Name);
    lcd.setCursor(0, 1);
    lcd.print(" Temp: ");
    lcd.print(DHT_T);
    lcd.print(" ");
    lcd.write((uint8_t)0);
    lcd.print("C");
  }
  else if(MenuMode == MenuHumidity) {
    // DHT Humidity display
    lcd.setCursor(0, 0);
    lcd.print("Humidity");
    lcd.setCursor(0, 1);
    lcd.print(DHT_H);
    lcd.print(" %");
  }
  else if(MenuMode == MenuCurrent) {
    // Watt + Ampere display
    lcd.setCursor(0, 0);
    lcd.print("Ampere: ");
    lcd.print(EnergyAmpere);
    lcd.setCursor(0, 1);
    lcd.print(" Watts: ");
    lcd.print(EnergyWatts);
  }
  else if (MenuMode == 15) {
    // testmode
     lcd.setCursor(0, 0);
     lcd.print(LCDMoment);
     lcd.setCursor(0, 1);
     lcd.print(LCDTimer);
     lcd.print(" ");
     lcd.print(CurrentTemps[0]);
  }
  else if(MenuMode == 16) {
    // test mode
    lcd.setCursor(0, 0);
    lcd.print(" Menumode: ");
    lcd.print(MenuMode);
    lcd.print("    ");
    lcd.setCursor(0, 1);
    lcd.print(LCDMoment - LCDTimer);
    lcd.print("   ");
  }
}

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

void loop(void) {
  // record current moments
  LCDMoment = SensorMoment = millis();

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

  // reset watchdog to keep us running
  wdt_reset();
  
  // finally, re-paint the screen
  screen();
  
  // for debugging, put current menu
  //lcd.setCursor(15, 1);
  //lcd.print(MenuMode);
}
