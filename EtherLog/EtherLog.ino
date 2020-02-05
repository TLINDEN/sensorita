// -*-c++-*-

/*
 *
 * very simple sketch, it reads from serial1 (5V TTL RX+TX)
 * and puts whatever it gets there out via http get request.
 *
 * Target platform: Arduino Leonardo
 *
 */

#include "SPI.h"
#include "avr/pgmspace.h"
#include "Ethernet.h"
#include "EthernetClient.h"
#include <stdio.h>

#include <avr/wdt.h>

// uncomment if running via crossover for local testing
//#define LOCAL


const char http_hostname[] = "www.daemon.de";
EthernetClient http_client;
char http_uri[80];
byte named[] = { 141,1,1,1 };
static uint8_t mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
unsigned long t = 0;

uint8_t onebyte = 0;
char parameter[80];
byte index = 0;

#ifndef LOCAL 
  byte ip[] = { 192,168,128,254 };
  byte gw[] = { 192,168,128,1   };
  IPAddress http_server(78,47,130,33);
#else  
  byte ip[] = { 10,1,1,2 };
  byte gw[] = { 10,1,1,1 };
  IPAddress http_server(10,1,1,1);
#endif

// WDT state
unsigned long WdtState = 0;

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

void reset_eth() {
  byte net[]= { 255, 255, 255, 0 };
  Ethernet.begin(mac, ip, named, gw, net);
}


void http_put (char uri[80]) {
  if (http_client.connect(http_server, 80)) {
    http_client.print("GET ");
    http_client.print(uri);
    http_client.println(" HTTP/1.0");
    http_client.print("Host: ");
    http_client.println(http_hostname);
    http_client.println("Connection: close");
    http_client.println();
    while(http_client.connected()) {
      while(http_client.available()) {
        // we've come so far, abort, we're not interested in any output for now
        http_client.stop();
      }
    }
    http_client.stop();
 } 
 else {
    // if you couldn't make a connection:
    http_client.stop();
  }
}

void setup () {
  Serial1.begin(9600);
  while (!Serial1) ;
  reset_eth();
  wdt_enable(WDTO_1S);
}



void checkrx() {
  while (Serial1.available() > 0) {
    // rx has data
    onebyte = Serial1.read();
    if(onebyte == '\r' || onebyte == '\n') {
      // logging sequence complete
      if(strlen(parameter) > 10) {
        http_put(parameter);
      }
      index        = 4;
      parameter[0] = '/';
      parameter[1] = 't';
      parameter[2] = 'd';
      parameter[3] = '/';
      parameter[4] = '\0';
    }
    else {
      parameter[index] = onebyte;
      index++;
      parameter[index] = '\0';
    }
  }
}

void loop () {
  // reset WDT state
  WdtState = 0x5555;
  wdt_a();

  // look for inputs
  checkrx();

  // hopefully reset WDT
  WdtState += 0x2222;
  wdt_b();
}
