/*********
  Basic implementation of a Nixie clock. The ESP8266 drives 3 74HC595 shift registers.
  Two of them are wired to 4 russian K155ND1 nixie tube drivers. The third one is used
  to drive decimal points and the second flasher.

  For some visual effects, there are also 4 WS2812 neopixel compatible LEDs, which create a 
  color fading effect
*********/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>

// define your network credentials in credentials.h
// const char* ssid = ....;
// const char* password = ....;
#include "credentials.h"

// define NTP server + timezone here
#define MY_NTP_SERVER "de.pool.ntp.org"           
#define MY_TZ "CET-1CEST,M3.5.0/02,M10.5.0/03"  

// The 74HC595 are wired to these pins
const int LATCH_PIN = 2;
const int CLOCK_PIN = 5;
const int DATA_PIN = 4;
 
const int NEOPIXEL_PIN = 15;

const int ESP_BUILTIN_LED = 2;

// init the neopixel strip
Adafruit_NeoPixel strip = Adafruit_NeoPixel(4, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
const int NUM_LEDS = strip.numPixels();
const uint32_t RED  = strip.Color(255, 0,   0);
const uint32_t BLUE = strip.Color(0,   0,   255);
const uint32_t ORANGE = strip.Color(255,   100,  32);


class Nixie {

  /**
   * Send one bit to the shift registers
   */
  void sendBit(int bit) {
    digitalWrite(DATA_PIN, !!bit);
    digitalWrite(CLOCK_PIN, HIGH);
    delayMicroseconds(45);
    digitalWrite(CLOCK_PIN, LOW);
    delayMicroseconds(45);
  }
  
  /**
   * Send one nibble (= 4 bit) to the shift registers
   */
  void sendNibble(int nibble) {
    sendBit(nibble & 8);
    sendBit(nibble & 4);
    sendBit(nibble & 2);
    sendBit(nibble & 1);
  }

  /**
   * send one digit to the shift registers.
   * Note: The inputs and outputs of the BCD2DECIMAL converters are
   * wired "random" to have no crossings. So we must do a translation here
   */
  void sendDigit(int digit) {
    switch (digit) {
      case 0: sendNibble(12); break;
      case 1: sendNibble(2); break;
      case 2: sendNibble(10); break;
      case 3: sendNibble(11); break;
      case 4: sendNibble(3); break;
      case 5: sendNibble(1); break;
      case 6: sendNibble(9); break;
      case 7: sendNibble(8); break;
      case 8: sendNibble(0); break;
      case 9: sendNibble(4); break;
      default: sendNibble(5); break;
    }
  }

  // These are the digit values that are currently displayed
  int realDigit[4] = {-1, -1, -1, -1};

  // This is used to implement the "slot machine effect", so that 
  // all nixie digits are active from time to time, so that they do not
  // degenerate
  void decDigit(int i) {
    if (digit[i] < 0 || digit[i] > 9) {
      return;
    }
    realDigit[i]--;
    if (realDigit[i] < 0) {
      realDigit[i] = 9;
    }
  }

public:
  int synced = 0;
  int digit[4] = {-1, -1, -1, -1};
  int dot[4] = {0, 0, 0, 0};
  int colon;
  void update() {
    int i;
    for (i = 0; i < 4; i++) {
      if (digit[i] < 0 || digit[i] > 9) {
        realDigit[i] = digit[i];
      }  
    }
    // check if we are in sync
    if (synced) {
      for (i = 0; i < 4; i++) {
        if (digit[i] != realDigit[i]) {
            decDigit(0);
            decDigit(1);
            decDigit(2);
            decDigit(3);
            synced = false;
            break;
        }  
      }
    } else {
      // propagate offs
      synced = true;
      for (i = 0; i < 4; i++) {
        if (digit[i] != realDigit[i]) {
            decDigit(i);
            synced = false;
            //break;
        }  
      }
    } 
    // Send data
    sendDigit(realDigit[3]); // third 74HC595 controls minute digits
    sendDigit(realDigit[2]);
    sendBit(dot[3] || !synced);         // second 74HC595 controls decimal dots
    sendBit(dot[2] || !synced);
    sendBit(colon);
    sendBit(dot[1] || !synced);
    sendBit(0);
    sendBit(0);
    sendBit(dot[0] || !synced);
    sendBit(0);
    sendDigit(realDigit[1]); // first 74HC595 controls hour digits
    sendDigit(realDigit[0]);
    digitalWrite(LATCH_PIN, HIGH);
    delayMicroseconds(45);
    digitalWrite(LATCH_PIN, LOW);
  }
};

Nixie nixie = Nixie();


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  configTime(MY_TZ, MY_NTP_SERVER);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed!");
  }

  //set pins to output so you can control the shift register
  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(ESP_BUILTIN_LED, OUTPUT);

  Serial.println("Ready");
  Serial.print("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  for (int i = 0; i < 4; i++) {
    nixie.digit[0] = ip[i] / 100;
    nixie.digit[1] = (ip[i] / 10) % 10;
    nixie.digit[2] = ip[i] % 10;
    nixie.dot[3] = true;
    do {
      nixie.update();
      delay(30);
    } while(!nixie.synced);
    delay(500);
  }
  nixie.dot[3] = false;

  
  strip.begin();
  strip.setBrightness(255);
  strip.show();
  digitalWrite(ESP_BUILTIN_LED, HIGH);
}


time_t now;                         // this is the epoch
tm tm; 

void printTime() {
  time(&now);                       // read the current time
  localtime_r(&now, &tm);           // update the structure tm with the current time
  Serial.print("year:");
  Serial.print(tm.tm_year + 1900);  // years since 1900
  Serial.print("\tmonth:");
  Serial.print(tm.tm_mon + 1);      // January = 0 (!)
  Serial.print("\tday:");
  Serial.print(tm.tm_mday);         // day of month
  Serial.print("\thour:");
  Serial.print(tm.tm_hour);         // hours since midnight  0-23
  Serial.print("\tmin:");
  Serial.print(tm.tm_min);          // minutes after the hour  0-59
  Serial.print("\tsec:");
  Serial.print(tm.tm_sec);          // seconds after the minute  0-61*
  Serial.print("\twday");
  Serial.print(tm.tm_wday);         // days since Sunday 0-6
  if (tm.tm_isdst == 1)             // Daylight Saving Time flag
    Serial.print("\tDST");
  else
    Serial.print("\tstandard");
  Serial.println();
}


unsigned long previousMillis1 = 0;  // will store last time LED was updated
unsigned long previousMillis2 = 0;  // will store last time LED was updated

// constants won't change:
const long interval = 1000;  // interval at which to blink (milliseconds)

int previousSecond;
uint32_t target_color = RED;
int second_counter;
void loop() {


  time(&now);                       // read the current time
  localtime_r(&now, &tm);           // update the structure tm with the current time

  unsigned long currentMillis = millis();
  
  if (previousSecond != tm.tm_sec) {
    // save the last time you blinked the LED
    previousSecond = tm.tm_sec;
    second_counter++;
    //printTime();
    if(previousSecond % 12 < 9){
      nixie.digit[0] = tm.tm_hour / 10;
      nixie.digit[1] = tm.tm_hour % 10;
      nixie.digit[2] = tm.tm_min / 10;
      nixie.digit[3] = tm.tm_min % 10;
      nixie.dot[2] = false;
      nixie.colon = !nixie.colon;
    } else {
      tm.tm_mon++;      
      nixie.digit[0] = tm.tm_mday / 10;
      nixie.digit[1] = tm.tm_mday % 10;
      nixie.digit[2] = tm.tm_mon / 10;
      nixie.digit[3] = tm.tm_mon % 10;
      nixie.dot[2] = true;
      nixie.colon = false;
    }
    switch (second_counter % 26) {
      case 0:
        target_color = BLUE;
        break;
      case 13:
        target_color = RED;
        break;
    }
  }
  if (currentMillis - previousMillis1 > 30) {
    previousMillis1 = currentMillis;
    nixie.update();
  }

  if (currentMillis - previousMillis2 > 30) {
    previousMillis2 = currentMillis;
  
    if (strip.getPixelColor(2) == target_color) strip.setPixelColor(3,fade(strip.getPixelColor(3), strip.getPixelColor(2)));
    if (strip.getPixelColor(1) == target_color) strip.setPixelColor(2,fade(strip.getPixelColor(2), strip.getPixelColor(1)));
    if (strip.getPixelColor(0) == target_color) strip.setPixelColor(1,fade(strip.getPixelColor(1), strip.getPixelColor(0)));
    strip.setPixelColor(0,fade(strip.getPixelColor(0), target_color));
    strip.show();
  }
}

union u32_2_byte{
   uint32_t u; 
   byte b[4] ;
};

uint32_t fade(uint32_t current, uint32_t target) {
  u32_2_byte u1, u2;
  int i, j;
  u1.u = current;
  u2.u = target;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 10; j++) {
      if (u1.b[i] < u2.b[i]) u1.b[i]++;
      if (u1.b[i] > u2.b[i]) u1.b[i]--;
    }
  }
  return u1.u;
}
// Fill strip pixels one after another with a color. Strip is NOT cleared
// first; anything there will be covered pixel by pixel. Pass in color
// (as a single 'packed' 32-bit value, which you can get by calling
// strip.Color(red, green, blue) as shown in the loop() function above),
// and a delay time (in milliseconds) between pixels.
void colorWipe(uint32_t color, int wait) {
  for(int i=0; i<strip.numPixels(); i++) { // For each pixel in strip...
    strip.setPixelColor(i, color);         //  Set pixel's color (in RAM)
    strip.show();                          //  Update strip to match
    delay(wait);                           //  Pause for a moment
  }
 }
