// Low-power ESP32 solution to autoregulate pendulum clocks per NTP, via a stepper motor to adjust pendulum
// https://github.com/clockspot/autoregulator
// Sketch by Luke McKenzie (luke@theclockspot.com)

#include <arduino.h>
#include "autoregulator.h" //specifies config
#include "esp_sleep.h"

//TODO which of these are needed for NTP sync
// #include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson needs version v6 or above
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h> // Needs to be from the ESP32 platform version 3.2.0 or later, as the previous has problems with http-redirect

#ifdef ENABLE_NEOPIXEL
  #include <Adafruit_NeoPixel.h>
  #define NUMPIXELS 1
  Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

unsigned long millisStart;

void setup() {

  millisStart = millis();

  // pinMode(RELAY_UNSET_PIN, OUTPUT);
  // pinMode(RELAY_SET_PIN, OUTPUT);
  // gpio_hold_dis(RELAY_UNSET_PIN);
  // gpio_hold_dis(RELAY_SET_PIN);
  // digitalWrite(RELAY_UNSET_PIN,LOW);
  // digitalWrite(RELAY_SET_PIN,LOW);

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
  //TODO save further power by leveraging Deep Sleep Wake Stub?
  //https://randomnerdtutorials.com/esp32-deep-sleep-arduino-ide-wake-up-sources/  
  
  #ifdef BATTERY_MONITOR_PIN
    pinMode(BATTERY_MONITOR_PIN, INPUT);
  #endif

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

  //Read battery voltage
  float battLevel = (analogRead(BATTERY_MONITOR_PIN) * 2.7) / 4096.0; //gives ~5v on USB

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

  //Who disturbs my slumber??
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_UNDEFINED) { //Cold start
    
    #ifdef SHOW_SERIAL
    Serial.println(F("Cold start, wait for new program"));
    #endif
    
    delay(3000); //let battery level indicator show for a bit

    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0x00FFFF); //teal to indicate cold start wait
      pixels.show();
    #endif

    delay(60000); //gives a chance to upload new sketch before it sleeps

    return;
    
  } //end cold start

  //otherwise we woke from sleep, probably by ESP_SLEEP_WAKEUP_EXT0

  #ifdef SHOW_SERIAL
  //Serial.printf("Wake from sleep at %lu",millisStart);
  Serial.println(F("Wake from sleep"));
  #endif

  //Start wifi
  for(int attempts=0; attempts<3; attempts++) {
    #ifdef SHOW_SERIAL
      Serial.print(F("\nConnecting to WiFi SSID "));
      Serial.println(NETWORK_SSID);
    #endif
    WiFi.begin(NETWORK_SSID, NETWORK_PASS);
    int timeout = 0;
    while(WiFi.status()!=WL_CONNECTED && timeout<15) {
      timeout++; delay(1000);
    }
    if(WiFi.status()==WL_CONNECTED){ //did it work?
      #ifdef ENABLE_NEOPIXEL
        pixels.fill(0x0000FF); //blue - wifi success
        pixels.show();
      #endif
      #ifdef SHOW_SERIAL
        Serial.println(F("Connected!"));
        //Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
        Serial.print(F("Signal strength (RSSI): ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
        Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());
      #endif
      break; //leave attempts loop
    }
  }
  if(WiFi.status()!=WL_CONNECTED) {
    #ifdef SHOW_SERIAL
      Serial.println(F("Wasn't able to connect."));
    #endif
    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0xFF0000); //red - no wifi success
      pixels.show();
      delay(1000);
    #endif

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  //If we reach this point, wifi is OK
  HTTPClient http;
  int httpReturnCode;
  for(int attempts=0; attempts<3; attempts++) {
    #ifdef SHOW_SERIAL
      Serial.print(F("\nSending to log, attempt "));
      Serial.println(attempts,DEC);
    #endif
    unsigned long offset = millis()-millisStart;
    http.begin(String(LOG_URL)+"&offset="+String(offset));
    httpReturnCode = http.GET();
    if(httpReturnCode==200) {
      #ifdef SHOW_SERIAL
        Serial.println(F("Successful!"));
      #endif
      #ifdef ENABLE_NEOPIXEL
        pixels.fill(0x00FF00); //green - log success
        pixels.show();
        delay(1000);
      #endif
      break; //leave attempts loop
    }
  }
  if(httpReturnCode!=200) {
    #ifdef SHOW_SERIAL
      Serial.print(F("Not successful. Last HTTP code: "));
      Serial.println(httpReturnCode,DEC);
    #endif
    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0xFF0000); //red - log failure
      pixels.show();
      delay(1000);
    #endif
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
    
} //end setup()

void loop() {
  //Once setup is done, quiet down and go to sleep
  Serial.flush();
  esp_deep_sleep_start();
}