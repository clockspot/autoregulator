// Low-power ESP32 solution to autoregulate pendulum clocks per NTP, via a stepper motor to adjust pendulum
// https://github.com/clockspot/autoregulator
// Sketch by Luke McKenzie (luke@theclockspot.com)

#include <arduino.h>
#include "autoregulator.h" //specifies config
#include "esp_sleep.h"


//TODO which of these are needed for NTP sync
// #include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson needs version v6 or above

// #include <WiFi.h>
// #include <WiFiClientSecure.h>
// #include <HTTPClient.h> // Needs to be from the ESP32 platform version 3.2.0 or later, as the previous has problems with http-redirect

#ifdef ENABLE_NEOPIXEL
  #include <Adafruit_NeoPixel.h>
  #define NUMPIXELS 1
  Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

#ifdef ENABLE_EINK
  #include "Adafruit_ThinkInk.h"
  #include <Fonts/FreeSans24pt7b.h>
  #include <Fonts/FreeSans18pt7b.h>
  #include <Fonts/FreeSansBold18pt7b.h>
  #include <Fonts/FreeSans12pt7b.h>
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSans9pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSans12pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSans18pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSans24pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold9pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold12pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold18pt7b.h 
  // /Users/luke/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts/FreeSansBold24pt7b.h
  ThinkInk_154_Tricolor_Z90 display(EPD_DC, EPD_RESET, EPD_CS, SRAM_CS, EPD_BUSY, EPD_SPI);
#endif

#ifdef ENABLE_MOTOR
  #include <Stepper.h>
  Stepper stepper(MOTOR_STEPS, MOTOR_A, MOTOR_B, MOTOR_C, MOTOR_D);
#endif

#ifdef ENABLE_DS3231
  // #include <Wire.h> //Arduino - GNU LPGL - for I2C access to DS3231
  // #include <DS3231.h> //NorthernWidget - The Unlicense - install in your Arduino IDE
  // DS3231 ds3231; //an object to access the ds3231 directly (temp, etc)
  // RTClib rtc; //an object to access a snapshot of the ds3231 via rtc.now()
  #include <RTClib.h>
  RTC_DS3231 rtc;
  DateTime tod;
#endif

unsigned long millisStart;

RTC_DATA_ATTR unsigned int counter = 0;

int displayY = 0;

void setup() {

  millisStart = millis();
  delay(500); //solves a bug of some kind
  //https://www.instructables.com/ESP32-Deep-Sleep-Tutorial/
  //https://simplyexplained.com/courses/programming-esp32-with-arduino/using-rtc-memory/

  counter++;

  #ifdef SHOW_SERIAL
    Serial.begin(115200);
    #ifdef SAMD_SERIES
      while(!Serial);
    #else
      delay(2000);
    #endif
    Serial.println(F("Hello world"));
  #endif

  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  gpio_hold_en(WAKEUP_PIN); //https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html#_CPPv316rtc_gpio_hold_en10gpio_num_t
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 0);
  // //TODO save further power by leveraging Deep Sleep Wake Stub?
  // //https://randomnerdtutorials.com/esp32-deep-sleep-arduino-ide-wake-up-sources/  
  
  #ifdef ENABLE_NEOPIXEL
    #if defined(NEOPIXEL_POWER)
      // If this board has a power control pin, we must set it to output and high
      // in order to enable the NeoPixels. We put this in an #if defined so it can
      // be reused for other boards without compilation errors
      pinMode(NEOPIXEL_POWER, OUTPUT);
      digitalWrite(NEOPIXEL_POWER, HIGH);
    #endif
    
    pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
    pixels.setBrightness(20); // not so bright
    pixels.fill(0xFFDD00); //yellow - indicate startup
    pixels.show();
  #endif

  #ifdef BATTERY_MONITOR_PIN
    pinMode(BATTERY_MONITOR_PIN, INPUT);
    //Read battery voltage

    // float battLevel = (analogRead(BATTERY_MONITOR_PIN) * 2.7) / 4096.0; //gives ~5v on USB

    #ifdef ENABLE_NEOPIXEL
    if(battLevel<3.5) {
      pixels.fill(0xFF5500); //orange - low battery
      pixels.show();
    }
    #endif

    #ifdef SHOW_SERIAL
    Serial.print(F("Battery: "));
    Serial.print(battLevel,DEC);
    if(battLevel<3.6) {
      Serial.println(F(" (low)"));
    } else {
      Serial.println(F(" (OK)"));
    }
    #endif
  #endif

  #ifdef ENABLE_DS3231
    // Wire.begin();
    rtc.begin();
    tod = rtc.now();
    
    #ifdef SHOW_SERIAL
      if(counter==1) {
        if(tod.hour()<10) Serial.print("0");
        Serial.print(tod.hour(),DEC);
        Serial.print(":");
        if(tod.minute()<10) Serial.print("0");
        Serial.print(tod.minute(),DEC);
        Serial.print(":");
        if(tod.second()<10) Serial.print("0");
        Serial.println(tod.second(),DEC);

        Serial.println("Enter 'c' to set real-time clock.");
      }
    #endif
  #endif

  #ifdef ENABLE_EINK
    #ifdef SHOW_SERIAL
      Serial.println(F("E-ink display enabled"));
    #endif
    display.begin(THINKINK_TRICOLOR);
    display.setRotation(2); //TODO return to 0

    if(counter==1) {
      display.clearBuffer();
      displayY = 0;
      
      display.setTextColor(EPD_BLACK);
      // display.setTextColor(EPD_RED);
      display.setFont(&FreeSansBold18pt7b);
      displayY += (18)*1.5;
      display.setCursor(0, displayY);
      display.print("Hello world!");
      #ifdef ENABLE_DS3231
        display.setFont(&FreeSans12pt7b);
        displayY += (6+12)*1.5;
        display.setCursor(0, displayY);
        display.print("Reference time:");

        display.setFont(&FreeSans18pt7b);
        displayY += (6+18)*1.5;
        display.setCursor(0, displayY);
        if(tod.hour()<10) display.print("0");
        display.print(tod.hour());
        display.print(":");
        if(tod.minute()<10) display.print("0");
        display.print(tod.minute());
        display.print(":");
        if(tod.second()<10) display.print("0");
        display.print(tod.second());

        display.setFont(&FreeSans12pt7b);
        displayY += (6+12)*1.5;
        display.setCursor(0, displayY);
        display.print("Enter 'c' to set.");
      #endif
      display.display();
    }
  #endif

  //Who disturbs my slumber??
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_UNDEFINED) { //Cold start
    
    #ifdef SHOW_SERIAL
    Serial.println(F("Cold start, wait for new program"));
    #endif
    
    // delay(3000); //let battery level indicator show for a bit

    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0x00FFFF); //teal to indicate cold start wait
      pixels.show();
    #endif

    // delay(60000); //gives a chance to upload new sketch before it sleeps
    return; //We'll let loop() do the delay stuff, so it can also set RTC, etc
    
  } //end cold start

  //otherwise we woke from sleep, probably by ESP_SLEEP_WAKEUP_EXT0

  #ifdef SHOW_SERIAL
  //Serial.printf("Wake from sleep at %lu",millisStart);
  Serial.println(F("Wake from sleep"));
  #endif

  // //Start wifi
  // for(int attempts=0; attempts<3; attempts++) {
  //   #ifdef SHOW_SERIAL
  //     Serial.print(F("\nConnecting to WiFi SSID "));
  //     Serial.println(NETWORK_SSID);
  //   #endif
  //   WiFi.begin(NETWORK_SSID, NETWORK_PASS);
  //   int timeout = 0;
  //   while(WiFi.status()!=WL_CONNECTED && timeout<15) {
  //     timeout++; delay(1000);
  //   }
  //   if(WiFi.status()==WL_CONNECTED){ //did it work?
  //     #ifdef ENABLE_NEOPIXEL
  //       pixels.fill(0x0000FF); //blue - wifi success
  //       pixels.show();
  //     #endif
  //     #ifdef SHOW_SERIAL
  //       Serial.println(F("Connected!"));
  //       //Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
  //       Serial.print(F("Signal strength (RSSI): ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
  //       Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());
  //     #endif
  //     break; //leave attempts loop
  //   }
  // }
  // if(WiFi.status()!=WL_CONNECTED) {
  //   #ifdef SHOW_SERIAL
  //     Serial.println(F("Wasn't able to connect."));
  //   #endif
  //   #ifdef ENABLE_NEOPIXEL
  //     pixels.fill(0xFF0000); //red - no wifi success
  //     pixels.show();
  //     delay(1000);
  //   #endif

  //   WiFi.disconnect(true);
  //   WiFi.mode(WIFI_OFF);
  //   return;
  // }

  // //If we reach this point, wifi is OK
  // HTTPClient http;
  // int httpReturnCode;
  // for(int attempts=0; attempts<3; attempts++) {
  //   #ifdef SHOW_SERIAL
  //     Serial.print(F("\nSending to log, attempt "));
  //     Serial.println(attempts,DEC);
  //   #endif
  //   unsigned long offset = millis()-millisStart;
  //   http.begin(String(LOG_URL)+"&offset="+String(offset));
  //   httpReturnCode = http.GET();
  //   if(httpReturnCode==200) {
  //     #ifdef SHOW_SERIAL
  //       Serial.println(F("Successful!"));
  //     #endif
  //     #ifdef ENABLE_NEOPIXEL
  //       pixels.fill(0x00FF00); //green - log success
  //       pixels.show();
  //       delay(1000);
  //     #endif
  //     break; //leave attempts loop
  //   }
  // }
  // if(httpReturnCode!=200) {
  //   #ifdef SHOW_SERIAL
  //     Serial.print(F("Not successful. Last HTTP code: "));
  //     Serial.println(httpReturnCode,DEC);
  //   #endif
  //   #ifdef ENABLE_NEOPIXEL
  //     pixels.fill(0xFF0000); //red - log failure
  //     pixels.show();
  //     delay(1000);
  //   #endif
  // }

  // WiFi.disconnect(true);
  // WiFi.mode(WIFI_OFF);

  //Here is where the magic happens

  // stepper.setSpeed(60);
  // stepper.step(dir?40:-40);
  // dir = (dir?0:1);

  #ifdef ENABLE_EINK
    display.clearBuffer();
    displayY = 0;
    
    display.setTextColor(EPD_BLACK);
    // display.setTextColor(EPD_RED);

    // display.setFont(&FreeSans24pt7b);
    // displayY += (24)*1.5;
    // display.setCursor(0, displayY);
    // display.print(counter);

    display.setFont(&FreeSans18pt7b);
    displayY += (18)*1.5;
    display.setCursor(0, displayY);
    display.print("Wakes: ");
    display.print(counter);

    // display.setFont(&FreeSans18pt7b);
    // displayY += (6+18)*1.5;
    // display.setCursor(0, displayY);
    // display.print(millisStart);

    #ifdef ENABLE_DS3231
      display.setFont(&FreeSans18pt7b);
      displayY += (6+18)*1.5;
      display.setCursor(0, displayY);
      if(tod.hour()<10) display.print("0");
      display.print(tod.hour());
      display.print(":");
      if(tod.minute()<10) display.print("0");
      display.print(tod.minute());
      display.print(":");
      if(tod.second()<10) display.print("0");
      display.print(tod.second());
    #endif

    display.display();

  #endif

  //Once setup is done, quiet down and go to sleep
  goToSleep();
    
} //end setup()


int inputStage = 0;
int incomingByte = 0;

void loop() {

  //Code for setting DS3231 from terminal
  #ifdef SHOW_SERIAL
    #ifdef ENABLE_DS3231
      if(Serial.available()>0) {
        String readString;
        while(Serial.available()) {
          char c = Serial.read();
          if(c!=10) readString += c;
          delay(2);
        }
        Serial.print("You entered string ");
        Serial.println(readString);
        if(readString=="c") inputStage=1;
        int incomingInt = readString.toInt();
        switch(inputStage) {
          case 1:
            Serial.println("Enter hour:");
            inputStage++;
            break;
          case 2:
            Serial.print("You entered int ");
            Serial.println(incomingInt,DEC);
            Serial.print("tod hour="); Serial.println(tod.hour(),DEC);
            // ds3231.setClockMode(false);
            // ds3231.setHour(incomingInt);
            rtc.adjust(DateTime(2025,6,12,incomingInt,0,0));
            tod = rtc.now();
            Serial.print("tod hour="); Serial.println(tod.hour(),DEC);
            Serial.println("Enter minute:");
            inputStage++;
            break;
          case 3:
            Serial.print("You entered ");
            Serial.println(incomingInt,DEC);
            // ds3231.setMinute(incomingInt);
            tod = rtc.now();
            rtc.adjust(DateTime(2025,6,12,tod.hour(),incomingInt,0));
            Serial.println("Enter second:");
            inputStage++;
            break;
          case 4:
            Serial.print("You entered ");
            Serial.println(incomingInt,DEC);
            // ds3231.setSecond(incomingInt);
            tod = rtc.now();
            rtc.adjust(DateTime(2025,6,12,tod.hour(),tod.minute(),incomingInt));
            Serial.print("Clock set to: ");
            tod = rtc.now();
            if(tod.hour()<10) Serial.print("0");
            Serial.print(tod.hour(),DEC);
            Serial.print(":");
            if(tod.minute()<10) Serial.print("0");
            Serial.print(tod.minute(),DEC);
            Serial.print(":");
            if(tod.second()<10) Serial.print("0");
            Serial.println(tod.second(),DEC);
            #ifdef ENABLE_EINK
              display.clearBuffer();
              displayY = 0;
              display.setTextColor(EPD_BLACK);
              display.setFont(&FreeSans18pt7b);
              displayY += 18*1.5;
              display.setCursor(0, displayY);
              display.print("Clock set to: ");
              displayY += (6+18)*1.5;
              display.setCursor(0, displayY);
              if(tod.hour()<10) display.print("0");
              display.print(tod.hour());
              display.print(":");
              if(tod.minute()<10) display.print("0");
              display.print(tod.minute());
              display.print(":");
              if(tod.second()<10) display.print("0");
              display.print(tod.second());
              display.display();
            #endif
            inputStage==0;
            break;
          default: break;
        }
      }
    #endif
  #endif

  if(millis()-millisStart>120000) goToSleep();

}

void goToSleep() {
  Serial.flush();
  esp_deep_sleep_start();
}