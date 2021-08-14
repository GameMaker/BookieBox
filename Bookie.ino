#include <FastLED.h>
/** 
  Implements the timers and LEDs for the Bookie Box device.

  Copyright (C) 2021  Aaron Cammarata

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
    I/O CONSTS
*/
#define NUM_LEDS 4
#define LED_DATA_OUT_PIN 2
#define MONITOR_PIN 3
#define SYSTEM_SWITCH_PIN 4
#define SWITCH_0_PIN 8
#define SWITCH_1_PIN 9
#define SWITCH_2_PIN 10
#define SWITCH_3_PIN 11

/**
   LED-related
*/
CRGB leds[NUM_LEDS];
unsigned long ledCounter = 0;
unsigned long ledSlowdownCounter = 0;
#define LED_SLOWDOWN_FACTOR 2
int ledTemp;
int InitHue = 42;
int InitSat = 255;
int WaitHue = 192;
int WaitSat = 255;
#define LED_FLASH_DELAY 150

/**
    Switch-related
*/
bool switches[4];
bool switchesLastFrame[4];

/**
   State logic
*/
enum State
{
  INIT = 1,          /* Waiting to start Setting the current time */
  SETTINGUP,         /* Setting up 'current time' */
  WAITING_FOR_BOOKS, /* After starting the timer, wait for all 4 books to be loaded */
  RUN_TIMERS         /* Waiting for next timer to go off */
};
State currentState;
unsigned long nextTimer = 0;
unsigned long nowMillis;
unsigned long lastMinuteMillis;
bool booksRead[4];
bool monitorOff = false;
#define SHOW_TIME_DELAY 3000
int time_hour = 1;
int time_min = 11;
unsigned long bookPickedUpTime = 0;
#define BOOK_READ_MIN_TIME 60000

/**
   SETUP
*/
void setup()
{
  pinMode(SWITCH_0_PIN, INPUT);
  pinMode(SWITCH_1_PIN, INPUT);
  pinMode(SWITCH_2_PIN, INPUT);
  pinMode(SWITCH_3_PIN, INPUT);
  pinMode(MONITOR_PIN, OUTPUT);
  pinMode(SYSTEM_SWITCH_PIN, INPUT);

  digitalWrite(MONITOR_PIN, LOW);

  FastLED.addLeds<PL9823, LED_DATA_OUT_PIN, RGB>(leds, NUM_LEDS);
  FastLED.clear();
  FastLED.show();

  currentState = INIT;

  switches[0] = switchesLastFrame[0] = booksRead[0] = false;
  switches[1] = switchesLastFrame[1] = booksRead[1] = false;
  switches[2] = switchesLastFrame[2] = booksRead[2] = false;
  switches[3] = switchesLastFrame[3] = booksRead[3] = false;

  Serial.begin(9600);
}

/**
   LOOP
*/
void loop()
{
  // Grab the current time - all timing should use nowMillis.
  nowMillis = millis();

  // Click the internal time forward if appropriate.
  advanceTime();
  
  // If the switch on the back is turned off, disable the rest of the logic.
  if (digitalRead(SYSTEM_SWITCH_PIN) == HIGH)
  {
    digitalWrite(MONITOR_PIN, LOW);
    fill_solid(leds, 4, CRGB(32, 32, 32));
    FastLED.show();
    delay(10);
    return;
  }

  // Turn off the relay if one of the books has timed out.
  if (monitorOff)
  {
    digitalWrite(MONITOR_PIN, HIGH);
  }
  else
  {
    digitalWrite(MONITOR_PIN, LOW);
  }

  // Create a counter for LED color cycling.
  ledSlowdownCounter++;
  if (ledSlowdownCounter % LED_SLOWDOWN_FACTOR == 0)
  {
    ledCounter++; /* Use this for color changes, etc. */
  }

  // Read the switches to detect whether any books are removed.
  GetSwitchState();
  switch (currentState)
  {
  case INIT:
  {
    // Waiting for first user input.
    if (switches[3])
    {
      currentState = SETTINGUP;
      nextTimer = nowMillis + SHOW_TIME_DELAY;
    }
    else
    {
      DoInitPulse();
    }
    break;
  }
  case SETTINGUP:
  {
    // Setting the internal time, since Arduino Uno R3 has no battery-backup clock 
    // or wifi for internet time.
    if (nowMillis > nextTimer)
    {
      ShowCurrentTime();
      nextTimer = nowmillis + SHOW_TIME_DELAY;
    }
    if (switches[0] && !switchesLastFrame[0])
    {
      // Increment hour
      time_hour++;
      if (time_hour == 24)
      {
        time_hour = 0;
      }
      FastLED.clear();
      leds[0] = CRGB::Purple;
      FastLED.show();
      delay(100);
      FastLED.clear();
      delay(100);
      nextTimer = nowmillis + SHOW_TIME_DELAY;
    }
    if (switches[1] && !switchesLastFrame[1])
    {
      // Increment minute tens
      time_min += 10;
      if (time_min >= 60)
      {
        time_min -= 60;
      }
      FastLED.clear();
      leds[1] = CRGB::Purple;
      FastLED.show();
      delay(100);
      FastLED.clear();
      delay(100);
      nextTimer = nowmillis + SHOW_TIME_DELAY;
    }
    if (switches[2] && !switchesLastFrame[2])
    {
      // Increment minute
      time_min++;
      if (time_min % 10 == 0)
      {
        if (time_min == 0)
        {
          time_min = 50;
        }
        else
        {
          time_min -= 10;
        }
      }
      FastLED.clear();
      leds[2] = CRGB::Purple;
      FastLED.show();
      delay(100);
      FastLED.clear();
      delay(100);
      nextTimer = nowmillis + SHOW_TIME_DELAY;
    }
    if (switches[3] && !switchesLastFrame[3])
    {
      // Done setting time - wait for user to put books in the shelf.
      lastMinuteMillis = nowMillis;
      currentState = WAITING_FOR_BOOKS;
    }
    DoSetupPulse();
    break;
  }
  case WAITING_FOR_BOOKS:
  {
    fill_solid(leds, 4, CRGB::Purple);
    if (switches[0] && switches[1] && switches[2] && switches[3] &&
        switchesLastFrame[0] && switchesLastFrame[1] && switchesLastFrame[2] && switchesLastFrame[3])
    {
      currentState = RUN_TIMERS;
    }
    break;
  }
  case RUN_TIMERS:
  {
    // Default state - set the LEDs based on timer state and whether each book has been read today.
    DoTimerPulse();
    DoTimerLogic();
    break;
  }
  }
  CopySwitchStateToLastFrame();
  FastLED.show();
}

/**
   The magic - if there's a timer almost ready, warn and then alarm on it, and eventually turn off the monitor.
*/
void DoTimerLogic()
{
  // If the book is picked up, start a timer
  if (!switches[0])
  {
    if (switchesLastFrame[0])
    {
      bookPickedUpTime = nowMillis;
    }
    else if (nowmillis > (bookPickedUpTime + BOOK_READ_MIN_TIME))
    {
      booksRead[0] = true;
    }
  }
  // If the book is picked up, start a timer
  if (!switches[1])
  {
    if (switchesLastFrame[1])
    {
      bookPickedUpTime = nowmillis;
    }
    else if (nowmillis > (bookPickedUpTime + BOOK_READ_MIN_TIME))
    {
      booksRead[1] = true;
    }
  }
  // If the book is picked up, start a timer
  if (!switches[2])
  {
    if (switchesLastFrame[2])
    {
      bookPickedUpTime = nowmillis;
    }
    else if (nowmillis > (bookPickedUpTime + BOOK_READ_MIN_TIME))
    {
      booksRead[2] = true;
    }
  }
  // If the book is picked up, start a timer
  if (!switches[3])
  {
    if (switchesLastFrame[3])
    {
      bookPickedUpTime = nowmillis;
    }
    else if (nowmillis > (bookPickedUpTime + BOOK_READ_MIN_TIME))
    {
      booksRead[3] = true;
    }
  }
}

/**
   Set leds to the init mode pulsing pattern
*/
void DoTimerPulse()
{
  monitorOff = false;

  /**
     Create a pulsing effect where each LED goes up/down in brightness, and
     each LED cycles offset from the one to the left.
  */

  ledTemp = ((ledCounter + 255) % 255);
  if (ledTemp > 127)
  {
    ledTemp = 255 - ledTemp;
  }
  ledTemp += 127;
  if (booksRead[0])
  {
    leds[0] = CHSV(96, 200, ledTemp);
  }
  else if (!switches[0])
  {
    leds[0] = CRGB::Black;
  }
  // if 6:30-6:55AM, warn on 0
  else if (time_hour == 6 && time_min >= 30 && time_min <= 54 && !booksRead[0])
  {
    leds[0] = CHSV(64, 255, ledTemp);
  }
  // if 6:55-7:00AM, alarm on 0
  else if (time_hour == 6 && time_min >= 55 && time_min <= 59 && !booksRead[0])
  {
    leds[0] = CHSV(0, 255, ledTemp);
  }
  // if 7:00 & not picked up recently, go solid red
  else if (time_hour >= 7 && !booksRead[0])
  {
    leds[0] = CHSV(0, 255, 255);
    monitorOff = true;
  }
  else
  {
    ledTemp -= 127;
    leds[0] = CHSV(WaitHue, WaitSat, ledTemp);
  }

  ledTemp = ((ledCounter + 205) % 255);
  if (ledTemp > 127)
  {
    ledTemp = 255 - ledTemp;
  }
  ledTemp += 127;
  if (booksRead[1])
  {
    leds[1] = CHSV(96, 200, ledTemp);
  }
  else if (!switches[1])
  {
    leds[1] = CRGB::Black;
  }
  // if 9:00-9:25AM, warn on 1
  else if (time_hour == 9 && time_min >= 0 && time_min <= 24 && !booksRead[1])
  {
    leds[1] = CHSV(64, 255, ledTemp);
  }
  // if 9:25-9:30AM, alarm on 1
  else if (time_hour == 9 && time_min >= 25 && time_min <= 29 && !booksRead[1])
  {
    leds[1] = CHSV(0, 255, ledTemp);
  }
  // if 9:30 & not picked up recently, go solid red
  else if (((time_hour > 9) || (time_hour == 9 && time_min >= 30)) && !booksRead[1])
  {
    leds[1] = CHSV(0, 255, 255);
    monitorOff = true;
  }
  else
  {
    ledTemp -= 127;
    leds[1] = CHSV(WaitHue, WaitSat, ledTemp);
  }

  ledTemp = ((ledCounter + 155) % 255);
  if (ledTemp > 127)
  {
    ledTemp = 255 - ledTemp;
  }
  ledTemp += 127;
  if (booksRead[2])
  {
    leds[2] = CHSV(96, 200, ledTemp);
  }
  else if (!switches[2])
  {
    leds[2] = CRGB::Black;
  }
  // if 11:30-11:55AM, warn on 2
  else if (time_hour == 11 && time_min >= 30 && time_min <= 54 && !booksRead[2])
  {
    leds[2] = CHSV(64, 255, ledTemp);
  }
  // if 11:55-12:00PM, alarm on 2
  else if (time_hour == 11 && time_min >= 55 && time_min <= 59 && !booksRead[2])
  {
    leds[2] = CHSV(0, 255, ledTemp);
  }
  // if 12:00 & not picked up recently, go solid red
  else if (time_hour >= 12 && !booksRead[2])
  {
    leds[2] = CHSV(0, 255, 255);
    monitorOff = true;
  }
  else
  {
    ledTemp -= 127;
    leds[2] = CHSV(WaitHue, WaitSat, ledTemp);
  }

  ledTemp = ((ledCounter + 105) % 255);
  if (ledTemp > 127)
  {
    ledTemp = 255 - ledTemp;
  }
  ledTemp += 127;
  if (booksRead[3])
  {
    //    Serial.println("b3read");
    leds[3] = CHSV(96, 200, ledTemp);
  }
  else if (!switches[3])
  {
    //    Serial.println("b3up");
    leds[3] = CRGB::Black;
  }
  // if 2:00-2:25PM, warn on 3
  else if (time_hour == 14 && time_min >= 0 && time_min <= 24 && !booksRead[3])
  {
    //    Serial.println("b3warn");
    leds[3] = CHSV(64, 255, ledTemp);
  }
  // if 2:25-2:30AM, alarm on 3
  else if (time_hour == 14 && time_min >= 25 && time_min <= 29 && !booksRead[3])
  {
    //    Serial.println("b3red");
    leds[3] = CHSV(0, 255, ledTemp);
  }
  // if 2:30 & not picked up recently, go solid red
  else if (((time_hour > 14) || (time_hour == 14 && time_min >= 30)) && !booksRead[3])
  {
    //    Serial.println("b3dead");
    leds[3] = CHSV(0, 255, 255);
    monitorOff = true;
  }
  else
  {
    //    Serial.println("b3wait");
    ledTemp -= 127;
    leds[3] = CHSV(WaitHue, WaitSat, ledTemp);
  }
}

/**
   Get switch state - returns an array of bools.
   Note these use a pull-up (down?) resistor - so "low" is 'pressed'.
*/
void GetSwitchState()
{
  if (digitalRead(SWITCH_0_PIN) == LOW)
  {
    //    Serial.println("s1H");
    switches[0] = true;
  }
  else
  {
    //    Serial.println("s1l");
    switches[0] = false;
  }
  if (digitalRead(SWITCH_1_PIN) == LOW)
  {
    switches[1] = true;
  }
  else
  {
    switches[1] = false;
  }
  if (digitalRead(SWITCH_2_PIN) == LOW)
  {
    switches[2] = true;
  }
  else
  {
    switches[2] = false;
  }
  if (digitalRead(SWITCH_3_PIN) == LOW)
  {
    //    Serial.println("s3H");
    switches[3] = true;
  }
  else
  {
    switches[3] = false;
  }
}

/**
   Copy the current state of switches to the "last frame" array, so we can detect up/down state change events.
*/
void CopySwitchStateToLastFrame()
{
  static int i;
  for (i = 0; i < 4; i++)
  {
    switchesLastFrame[i] = switches[i];
  }
}

/**
   Set leds to the init mode pulsing pattern
*/
void DoInitPulse()
{
  /**
     Create a pulsing effect where each LED goes up/down in brightness, and
     each LED cycles offset from the one to the left.
  */
  ledTemp = ((ledCounter + 255) % 512);
  if (ledTemp > 255)
  {
    ledTemp = 255 - ledTemp;
  }
  leds[0] = CHSV(InitHue, InitSat, ledTemp);
  ledTemp = ((ledCounter + 155) % 512);
  if (ledTemp > 255)
  {
    ledTemp = 255 - ledTemp;
  }
  leds[1] = CHSV(InitHue, InitSat, ledTemp);
  ledTemp = ((ledCounter + 55) % 512);
  if (ledTemp > 255)
  {
    ledTemp = 255 - ledTemp;
  }
  leds[2] = CHSV(InitHue, InitSat, ledTemp);
  ledTemp = ((ledCounter - 45) % 512);
  if (ledTemp > 255)
  {
    ledTemp = 255 - ledTemp;
  }
  leds[3] = CHSV(InitHue, InitSat, ledTemp);
}

/**
   Set leds to the setup mode pulsing pattern
*/
void DoSetupPulse()
{
  /**
     Create a pulsing effect where each LED goes up/down in brightness, and
     each LED cycles offset from the one to the left.
  */
  ledTemp = (ledCounter % 255);
  fill_solid(leds, 4, CHSV(ledTemp, 255, 120));
}

/**
   Show current time:
   Flash each one in sequence showing what we think is the current time of day.
   First light is hours, second is minutes.
*/
void ShowCurrentTime()
{
  int hoursToFlash = time_hour;
  if (time_hour >= 12)
  {
    hoursToFlash -= 12;
  }
  int tenMinutesToFlash = time_min / 10;
  int minutesToFlash = time_min % 10;

  //  Serial.println("Flashing");
  //  Serial.println(hoursToFlash);
  //  Serial.println(tenMinutesToFlash);
  //  Serial.println(minutesToFlash);
  FastLED.clear();
  FastLED.show();
  int i;
  for (i = 0; i < hoursToFlash; i++)
  {
    if (time_hour < 12)
    {
      leds[0] = CRGB::MediumBlue;
    }
    else
    {
      leds[0] = CRGB::Yellow;
    }
    FastLED.show();
    delay(LED_FLASH_DELAY);
    FastLED.clear();
    FastLED.show();
    delay(LED_FLASH_DELAY);
  }
  delay(LED_FLASH_DELAY * 2);
  for (i = 0; i < tenMinutesToFlash; i++)
  {
    leds[1] = CRGB::MediumBlue;
    FastLED.show();
    delay(LED_FLASH_DELAY);
    FastLED.clear();
    FastLED.show();
    delay(LED_FLASH_DELAY);
  }
  delay(LED_FLASH_DELAY * 2);
  for (i = 0; i < minutesToFlash; i++)
  {
    leds[2] = CRGB::MediumBlue;
    FastLED.show();
    delay(LED_FLASH_DELAY);
    FastLED.clear();
    FastLED.show();
    delay(LED_FLASH_DELAY);
  }
}

void advanceTime()
{
  if (nowMillis - lastMinuteMillis > 60000)
  {
    incrementMinute();
    lastMinuteMillis += 60000;
  }
}

void incrementMinute()
{
  time_min++;
  if (time_min == 60)
  {
    time_min = 0;
    time_hour++;
  }
  if (time_hour == 24)
  {
    time_hour = 0;
    // Reset the timers at midnight
    if (time_hour == 0 && time_min == 0)
    {
      booksRead[0] = false;
      booksRead[1] = false;
      booksRead[2] = false;
      booksRead[3] = false;
    }
  }
}