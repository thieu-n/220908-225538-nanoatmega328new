#include "Arduino.h"
#include <TM1637TinyDisplay6.h>
#include <EEPROM.h>

//atmega 328 bootloader, not old version

void EEPROMWritelong(int address, long value);
long EEPROMReadlong(long address);
void InterruptFunction1();
void InterruptFunction2();
void reset_disps();
void blink(int displNo);
void updateDisplays(bool disp1Fault, bool disp2Fault);




// TODO: 
// 


#define STOP_ON_FAULT_PICKUP 0   // determines in normal mode whether the program is stopped when a team glass is lifted before the raf glass is lifted
#define STOP_ON_FAULT_PUTDOWN 0   // determines in normal mode whether the program is stopped when a team glass is lifted before the raf glass is put down
int MINTIME = 10000;     // fake constant for minimal time before team can stop, gets changed for the "duel" mode
#define FLASHSPEED 100    // ms speed for blinking the LED
#define FAULTSPEED 100    // ms speed for blinking the fault LED

//#define ledPin 2
#define ledPin 5
#define RESETPIN 6

#define in1 3
#define CLK1 A5
#define DIO1 A4

//#define in2 4
#define in2 2
#define CLK2 A3
#define DIO2 A2

#define in3 4

const uint8_t DISPLAYBRIGHTNESS[2] = {0,7};
bool displayBrightnessBool = 1;       //what is the brightness the display should have
bool changeDispBrightness = 0;        // should the brightness be changed?


TM1637TinyDisplay6 display1(CLK1, DIO1);
TM1637TinyDisplay6 display2(CLK2, DIO2);


uint8_t data[] = { 0, 0, 0, 0, 0, 0};

unsigned long startMillis = 0;    //Starting time of counter
volatile long totTime1 = 3140;      //Length of adt, =millis - starttime
volatile long totTime2 = 3140;      //Length of adt, =millis - starttime
unsigned long liftTime = 0;
unsigned long placeTime = 0;
unsigned long currentTime = 0;
unsigned long blinkTime = 0;
unsigned long totTimes[2] = {3140, 3140};

volatile bool stop1 = 0;    //gets called in interrupt function
volatile bool stop2 = 0;
volatile bool stop15 = 0;    //For the practice function
volatile bool stop25 = 0;
bool exit1Flag = 0;         //flag for when all glasses are placed
bool exit3Flag = 0;
bool fault1 = 0;            // team 1 has made an error
bool fault2 = 0;
bool faultFlash1 = 0;       // bool for if flash needs to be high or low on fault
bool faultFlash2 = 0;
unsigned long blinkStartMillis = 0;

int timerMode = 0;
/*
    mode 0 is the regular mode
    mode 3 is the selection mode, to go to mode 4 or 5
    mode 4 is the duel mode. This changes the minimal round time from 10 seconds to 1 second, for 1- on 1
    mode 5 is the pickup practice mode. This shows how much time there is between the ref placing the glass and player putting glass down
    mode 1 and 2 are sub-modes for mode 5 for when a player makes a fault
*/


void setup() {
  pinMode(in1, INPUT_PULLUP);
  pinMode(in2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(in1), InterruptFunction1, FALLING);
  attachInterrupt(digitalPinToInterrupt(in2), InterruptFunction2, FALLING);
  pinMode(in3, INPUT_PULLUP);
  pinMode(RESETPIN, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  display1.setBrightness(BRIGHT_HIGH);
  display2.setBrightness(BRIGHT_HIGH);
  totTime1 = EEPROMReadlong(0);
  totTime2 = EEPROMReadlong(4);
  display1.showNumberDec(totTime1, 0b00100000, true, 6);  // time in ms
  display2.showNumberDec(totTime2, 0b00100000, true, 6);  // time in ms
  //Serial.begin(9600);
  if (!digitalRead(RESETPIN)) {
    timerMode = 3;
  }
  // while(1){
  //   //updateDisplays(0,0);
  // delay(100);
  // display1.showNumberDec(totTime1, 0b00100000, true, 6);
  // delay(100);
  // display2.showNumberDec(totTime2, 0b00100000, true, 6);
  // }
}

void loop() {
  //Serial.println("started"); 
  Start:      // declaration for goto
  switch (timerMode) {
    case 0:     
      // start state: last times are displayed, wait for reset, flash screens of team that made a misstake 
      digitalWrite(ledPin, 0);  // turn readyled off
      while (digitalRead(RESETPIN)) { // wait until timer is RESETPIN
        updateDisplays(fault1,fault2);
      }

      // if the time is reset, do the following
      reset_disps();

      placeTime = millis(); // last time at which all glasses were not placed
      while (!exit1Flag) {    // wait for all glasses and 0.5 seconds
        if (!digitalRead(in1) && !digitalRead(in2) && !digitalRead(in3)) {       //if all glasses are placed
          if (millis() - placeTime > 500) { // and 0.5 sec has passed
            exit1Flag = 1;
            digitalWrite(ledPin, 1);  // turn readyled on
          }
        } else {
          placeTime = millis(); // last time at which all glasses were not placed
        }
      }
      exit1Flag = 0;  // reset exitflag for next round

      while (!digitalRead(in3)) {  // wait for ref to pick up glass
        updateDisplays(fault1,fault2);
        if(!digitalRead(RESETPIN)){
          reset_disps();
          goto Start;
        }
      }
      // here, the ref has picked up his glass
      digitalWrite(ledPin, 0);  // turn readyled off
      liftTime = millis();
      while (digitalRead(in3) || millis() - liftTime < 500) {  // wait for ref to place glass
        updateDisplays(fault1,fault2);
        if(digitalRead(in1)){
          fault1 = 1;
        }
        if(digitalRead(in2)){
          fault2 = 1;
        }
        if(!digitalRead(RESETPIN)){
          reset_disps();
          goto Start;
        }
      }

      stop1 = 0;  // reset stopflags for interrupts
      stop2 = 0;
      startMillis = millis();   // set time of starting match
      while (!(stop1 && stop2)) {    //the timer has started and is counting up, keep going till the interrupt functions sets both stop flags to 1, or reset
        currentTime = millis() - startMillis;   // timer for displaying
        if (!stop1) { // if the interrupt-flag has not been set, keep updating the time
          totTime1 = currentTime;  
        }
        if (!stop2) { //
          totTime2 = currentTime;
        }
        updateDisplays(fault1,fault2);
        if (!digitalRead(RESETPIN)) { // if RESETPIN button is pressed
          reset_disps();
          goto Start;
        }

        if (millis() - blinkTime > FLASHSPEED) {    // blink LED
          blinkTime = millis();
          digitalWrite(ledPin, !digitalRead(ledPin));
        }
      }
      break;



    case 1:   // team 1 has made fault, this is used in mode 5
      while (digitalRead(RESETPIN)) {   // while RESETPIN is not pressed
        if (millis() - blinkTime > FAULTSPEED) {    // blink
          blinkTime = millis();
          if (faultFlash1) {
            display1.setBrightness(BRIGHT_HIGH);
            faultFlash1 = 0;
          } else {
            display1.setBrightness(BRIGHT_LOW);
            faultFlash1 = 1;
          }
          display1.showNumberDec(totTime1, 0b00100000, true, 6);  // time in ms
        }
      }
      display1.setBrightness(BRIGHT_HIGH);
      display1.showNumberDec(totTime1, 0b00100000, true, 6);  // time in ms
      timerMode = 5;
      break;

    case 2:  // team 2 has made fault,  this is used in mode 5
      while (digitalRead(RESETPIN)) {   // while RESETPIN is not pressed
        if (millis() - blinkTime > FAULTSPEED) {    // blink
          blinkTime = millis();
          if (faultFlash2) {
            display2.setBrightness(BRIGHT_HIGH);
            faultFlash2 = 0;
          } else {
            display2.setBrightness(BRIGHT_LOW);
            faultFlash2 = 1;
          }
          display2.showNumberDec(totTime2, 0b00100000, true, 6);  // time in ms
        }
      }
      display2.setBrightness(BRIGHT_HIGH);
      display2.showNumberDec(totTime2, 0b00100000, true, 6);  // time in ms
      timerMode = 5;
      break;


    case 3: // select new mode
      display1.showString("MMode:"); // Test literal string
      display2.clear();
      while (!exit3Flag) {
        if (!digitalRead(in1)) {
          timerMode = 4;
          display2.showString("Duel");
          delay(1000);
          exit3Flag = 1;
        } else if (!digitalRead(in2)) {
          timerMode = 5;
          display2.showString("Pract");
          delay(1000);
          exit3Flag = 1;
        }
      }
      exit3Flag = 0;
      break;

    case 4: // ----------------------------------------------------     Duel mode       -----------------------------------------------------------------------
      MINTIME = 1000; // set the minimal time for ending an Adt to 1 second instead of 10
      timerMode = 0;  // return to the normal timer
      break;

    case 5: //  -----------------------------------------------------    Pickup practice mode   -----------------------------------------------------------------------
      while (digitalRead(RESETPIN)) { // wait until timer is reset
        updateDisplays(fault1,fault2);
      }
      reset_disps();
      updateDisplays(fault1,fault2);
      placeTime = millis();
      while (!exit1Flag) {
        if (!digitalRead(in1) && !digitalRead(in2) && !digitalRead(in3)) {       //if all glasses are placed
          if (millis() - placeTime > 500) { // and 0.5 sec has passed
            exit1Flag = 1;
            digitalWrite(ledPin, 1);  // turn readyled on
          }
        } else {
          placeTime = millis();
        }
      }
      exit1Flag = 0;
      totTime1 = 0;
      totTime2 = 0;
      while (!digitalRead(in3)) {  // wait for ref to pick up glass, if any player makes a misstake, go to mode 1/2 to display this
        if (digitalRead(in1)) {
          fault1 = 1;
        }
        if (digitalRead(in2)) {
          fault2 = 1;
        }
      }
      digitalWrite(ledPin, 0);  // turn readyled off when glass is picked up

      liftTime = millis();
      while (digitalRead(in3) || millis() - liftTime < 500) {  // wait for ref to place glass if any player makes a misstake, go to mode 1/2 to display this
        if (digitalRead(in1)) {
          fault1 = 1;
        }
        if (digitalRead(in2)) {
          fault2 = 1;
        }
        updateDisplays(fault1,fault2);
      }
      stop15 = 0;  // reset stopflags, use separate stopflags as in this case the lifting of the glasses is measured, not the placing
      stop25 = 0;
      totTime1 = 0;
      totTime2 = 0;
      startMillis = millis();
      while (!(stop15 && stop25)) {    //while both stop flags have not been set
        currentTime = millis() - startMillis;       //
        if(digitalRead(in1)){
          stop15 = 1;
        }        
        if(digitalRead(in2)){
          stop25 = 1;
        }

        if (!stop15) {     // stop when glass is lifted
          totTime1 = currentTime;
        }
        if (!stop25) {    // stop when glass is lifted
          totTime2 = currentTime;
        }
        if (!digitalRead(RESETPIN)) { // if reset button is pressed, just reset everything
          reset_disps();
          goto Start;     // start over from the top
        }

        if (millis() - blinkTime > FLASHSPEED) {    // blink LED
          blinkTime = millis();
          digitalWrite(ledPin, !digitalRead(ledPin));
        }
        updateDisplays(fault1,fault2);
      }
      digitalWrite(ledPin, 0);
      break;
      }
  }


void InterruptFunction1() {
  if (millis() - startMillis > MINTIME && stop1 == 0) {  // if the minimum time has passed, and this timer was not yet stopped
    totTime1 = millis() - startMillis;
    stop1 = 1;
    EEPROMWritelong(0, totTime1); //Set the time in memory
  }
}

void InterruptFunction2() {
  if (millis() - startMillis > MINTIME && stop2 == 0) {
    totTime2 = millis() - startMillis;
    stop2 = 1;
    EEPROMWritelong(4, totTime2); //Set the time in memory
  }
}

void reset_disps(){
  totTime1 = 0;
  totTime2 = 0;
  fault1 = 0;
  fault2 = 0;
  display1.setBrightness(BRIGHT_HIGH);
  display2.setBrightness(BRIGHT_HIGH);
  display1.showNumberDec(totTime1, 0b00100000, true, 6);  // time in ms
  display2.showNumberDec(totTime2, 0b00100000, true, 6);  // time in ms
  digitalWrite(ledPin, 0);  // turn readyled off
}

void updateDisplays(bool disp1Fault, bool disp2Fault){
  if(millis() - blinkStartMillis > FAULTSPEED){
    blinkStartMillis = millis();
    displayBrightnessBool = !displayBrightnessBool;
      if(disp1Fault){
        Serial.print(" 1 fault");
        display1.setBrightness(DISPLAYBRIGHTNESS[displayBrightnessBool],1);
      }
      if(disp2Fault){
        Serial.print(" 2 fault");
        display2.setBrightness(DISPLAYBRIGHTNESS[displayBrightnessBool],1);
      }
      Serial.println();
  }
  display1.showNumberDec(totTime1, 0b00100000, true, 6);
  display2.showNumberDec(totTime2, 0b00100000, true, 6);
}


void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

//This function will return a 4 byte (32bit) long from the eeprom
//at the specified address to address + 3.
long EEPROMReadlong(long address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
