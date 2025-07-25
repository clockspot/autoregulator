// Low-power ESP32 solution to autoregulate pendulum clocks per NTP, via a stepper motor to adjust pendulum
// https://github.com/clockspot/autoregulator
// Sketch by Luke McKenzie (luke@theclockspot.com)

// TODO why does it wake up so erratically
// TODO From wake 0 to 1, should still be able to calculate period vs target - TODO actually must document that wake 1 must occur correctly, neither skipped nor spurious
// TODO at startup, move off stop, so that test move is always effective; don't monitor position, and just detect when change was less than effective
// TODO Period to Per.
// TODO get temp from DS3231
// TODO get decimal from DS3231

#include <arduino.h>
#include "autoregulator.h" //specifies config
#include "esp_sleep.h"


#ifdef ENABLE_NEOPIXEL
  #include <Adafruit_NeoPixel.h>
  #define NUMPIXELS 1
  Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

#ifdef ENABLE_EINK
  #include "Adafruit_ThinkInk.h"
  // #include <Fonts/FreeSansBold24pt7b.h>
  // #include <Fonts/FreeSans24pt7b.h>
  // #include <Fonts/FreeSansBold18pt7b.h>
  // #include <Fonts/FreeSans18pt7b.h>
  #include <Fonts/FreeSansBold12pt7b.h>
  #include <Fonts/FreeSans12pt7b.h>
  // #include <Fonts/FreeSansBold9pt7b.h>
  #include <Fonts/FreeSans9pt7b.h>
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
#endif

#ifdef ENABLE_WIFI
  #include <WiFi.h>
  //TODO which of these are needed for NTP sync
  // #include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson needs version v6 or above
  // #include <WiFiClientSecure.h>
  #include <HTTPClient.h> // Needs to be from the ESP32 platform version 3.2.0 or later, as the previous has problems with http-redirect
  #define ENABLE_LOG
#endif

#ifdef SHOW_SERIAL
  #ifndef ENABLE_LOG
    #define ENABLE_LOG
  #endif
#endif

#ifdef ENABLE_LOG
  String logMsg;
  //TODO replace with char arrays?
#endif

unsigned long millisStart;

RTC_DATA_ATTR unsigned long triggerCount = 0;
  //using unsigned long because, why not, if we have the space? Allows for up to 4,294,967,295 samples, which is enough for hourly samples for 500,000 years. There are only 8760 hourly samples in a year so could reasonably go with word (65535) if needed.
RTC_DATA_ATTR long motorCur = 0; //where the regulator motor currently is vs MOTOR_MAX
//TODO set up the reset button to trigger a resetMotor() during the first wake
RTC_DATA_ATTR unsigned long refPrev = 0; //Reference time at most recent sample - millis per day (86400000). Known by trigger 2.
RTC_DATA_ATTR long ratePrev = 0; //Rate for the period between these reference times - gain/loss in millis per real period (hour=3600000). Known by trigger 3.
RTC_DATA_ATTR int adjRateFactor = 0; //How much an adjustment of MOTOR_STEPS should be expected to affect the rate
RTC_DATA_ATTR int adjRegPrev = 0; //adj intended to correct rate
RTC_DATA_ATTR int adjOffPrev = 0; //adj intended to correct offset from reference time by next sample, which will reverse this

int displayY = 0;

unsigned long ref = 0; //We will populate this with a reference time, either from RTC or NTP, and backdate it by the time it took to get it (100% of the time to when we start the request, and in the case of NTP, 50% of the time it takes to get the request back), so this will represent as accurately as possible the moment when the clock triggered it

void setup() {

  millisStart = millis();
  delay(500); //solves a bug of some kind
  //https://www.instructables.com/ESP32-Deep-Sleep-Tutorial/
  //https://simplyexplained.com/courses/programming-esp32-with-arduino/using-rtc-memory/

  // counter++;

  #ifdef SHOW_SERIAL
    Serial.begin(115200);
    #ifdef SAMD_SERIES
      while(!Serial);
    #else
      // delay(100);
    #endif
    // Serial.println(F("Hello world"));
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
    //Time to work out what RTC was at boot, to millisecond precision. Example:
    //We booted when RTC sec was 25.6; we captured millis() at that time.
    //When we get here, RTC sec reads 28 (actual 28.3); millis since boot is 2700 (28.3-25.6=2.7s)
    //We keep checking RTC til second changes to 29; millis since boot is now 3400 (29.0-25.6=3.4s)
    //So we can now take that RTC time and subtract the millis since boot to get RTC sec = 25.6.
    DateTime todA;
    DateTime todB;
    todA = rtc.now();
    while(1) { //Take up to one second to detect RTC second change
      todB = rtc.now();
      if(todA.second()!=todB.second()) break;
    }
    //Start with negative millis since boot (assuming that gets evaluated first)
    //and add millis for each of the time of day components as of the last RTC sample
    ref = 0-(millis()-millisStart)+(todB.hour()*3600000)+(todB.minute()*60000)+(todB.second()*1000);
  #endif
  //TODO else if no DS3231, get time from wifi
  //in that case ref should be something like (millis2-millis1)+((millis3-millis2)/2), where the "rtc" is halfway between when we started the request and when we got it back

  #ifdef ENABLE_EINK
    display.begin(THINKINK_TRICOLOR);
    display.setRotation(EINK_ROTATION);
    display.clearBuffer();
    //the display contents will be built up procedurally as we go, like serial output
  #endif

  motorCur = MOTOR_MAX/2; //assume it's somewhere in the middle, until it is autocalibrated
  //If the motor is not enabled, it will just pretend to drive one and display the results
  #ifdef ENABLE_MOTOR
    stepper.setSpeed(60);
  #endif

  #ifdef ENABLE_WIFI
    //Start wifi
    WiFi.mode(WIFI_STA);
    for(int attempts=0; attempts<3; attempts++) {
      #ifdef SHOW_SERIAL
        Serial.print(F("Connecting to WiFi SSID "));
        Serial.println(WIFI_SSID);
      #endif
      WiFi.begin(WIFI_SSID, WIFI_PASS);
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
        //don't display anything on the e-ink
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
      #ifdef ENABLE_EINK
        display.setTextColor(EPD_RED);
        display.setFont(&FreeSans12pt7b);
        displayY += (12)*1.5; display.setCursor(0, displayY);
        display.print("WiFi failed.");
      #endif
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  #endif

  //Who disturbs my slumber??
  if(esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_EXT0) { //Cold start
    //Does this make a difference vs. ==ESP_SLEEP_WAKEUP_UNDEFINED ?
    //TODO could also just be based on triggerCount=0
    
    #ifdef ENABLE_LOG
      logMsg.concat("Wake=0");
      logMsg.concat("&Ref=");
      logMsg.concat(formatTOD(ref,1));
    #endif

    #ifdef SHOW_SERIAL
      delay(2000);
      Serial.print(F("Cold start. Ref time is "));
      Serial.println(formatTOD(ref,1));
      Serial.println(F("Enter 'w' to stay awake."));
      Serial.println(F("Enter 's' to enter deep sleep."));
      #ifdef ENABLE_DS3231
        Serial.println("Enter 'c' to set real-time clock.");
      #endif
      Serial.println(F("Enter 'm' to arbitrarily move motor."));
      Serial.println(F("Enter 'a' to autocalibrate motor."));
    #endif
  
    #ifdef BATTERY_MONITOR_PIN
      delay(3000); //let battery level indicator show for a bit
    #endif

    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0x00FFFF); //teal to indicate cold start wait
      pixels.show();
    #endif

    #ifdef ENABLE_EINK
      display.setTextColor(EPD_BLACK);
      
      display.setFont(&FreeSansBold12pt7b);
      displayY += (12)*1.5; display.setCursor(0, displayY);
      display.print("Autoregulator");

      display.setFont(&FreeSans12pt7b);
      displayY += (6+12)*1.5; display.setCursor(0, displayY);
      display.print("by @clockspot");

      // displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
      // display.print("Calibrate motor: r"); //TODO
      // display.print("Press BOOT to reset motor."); //TODO

      display.setFont(&FreeSans12pt7b);
      displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
      display.print("Time ");
      display.setFont(&FreeSansBold12pt7b);
      display.print(formatTOD(ref,1));

      display.setFont(&FreeSans12pt7b);
      displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
      display.print("Awaiting trigger.");

      display.display();
    #endif

    return; //We'll let loop() monitor for serial inputs and leave time for a new sketch upload
    
  } //end cold start

  //otherwise we woke from sleep, probably by ESP_SLEEP_WAKEUP_EXT0
  //in this part, the log string is written to both serial and wifi log

  triggerCount++;

  #ifdef ENABLE_LOG
    logMsg.concat("Wake=0");
    logMsg.concat("&Ref=");
    logMsg.concat(formatTOD(ref,1));
  #endif

  #ifdef ENABLE_EINK
    display.setTextColor(EPD_BLACK);
    
    display.setFont(&FreeSans12pt7b);
    displayY += (12)*1.5; display.setCursor(0, displayY);
    display.print("Wake ");
    display.setFont(&FreeSansBold12pt7b);
    display.print(triggerCount);

    display.setFont(&FreeSans12pt7b);
    displayY += (6+12)*1.5; display.setCursor(0, displayY);
    display.print("Time ");
    display.setFont(&FreeSansBold12pt7b);
    display.print(formatTOD(ref,1));
  #endif

  if(triggerCount==1) {
    //first wake - we have nothing
    #ifdef ENABLE_LOG
      logMsg.concat("&Msg=At next wake, we will know rate.");
    #endif
    #ifdef ENABLE_EINK
      display.setFont(&FreeSans12pt7b);
      displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
      display.print("At next wake,");

      displayY += (6+12)*1.5; display.setCursor(0, displayY);
      display.print("we'll know rate.");
    #endif

  } else {
    //second+ wake - we have refPrev and can calculate rate
    long period = ref - refPrev; //should be ~3600000
    if(period<0) period+=86400000; //midnight rollover

    //Once the clock is well-regulated, it may be many samples before the difference is appreciable,
    //because of tolerances in the trigger (a few ticks early or late).
    //TODO: average the samples, even if they are at a delay?

    /*    
    We want to tolerate missed real samples, so we add a multiplication factor to the target.
    multiplication factor = (period+(target/2))/target
    the +(target/2) turns the decimal truncation into a rounding
    Examples:
    one hour: (3600000+(1800000))/3600000 = 1(.5)
    1.1 hour: (3960000+(1800000))/3600000 = 1(.6)
    1.5 hour: (5400000+(1800000))/3600000 = 2
    1.6 hour: (5760000+(1800000))/3600000 = 2(.1)
    2.0 hour: (7200000+(1800000))/3600000 = 2(.5)
    */
    int targetx = (period+(PERIOD_MILS/2))/PERIOD_MILS;
    long target = PERIOD_MILS*targetx;
    /*
    We also want to discard samples that are spurious, 
    so we discard if more than 1% out of target.
    To calculate percentage to 4 places, we divide period by (target/10000)
    Examples:
    60min   = 3600000/360 = 10000 = valid   (100.00% of 1 hour)
    59.5min = 3570000/360 =  9916 = valid   ( 99.16% of 1 hour)
    59min   = 3540000/360 =  9830 = invalid ( 98.33% of 1 hour)
    120min  = 7200000/720 = 10000 = valid   (100.00% of 2 hours)
    119min  = 7140000/720 =  9916 = valid   ( 99.16% of 2 hours)
    118min  = 7080000/720 =  9833 = invalid ( 98.33% of 2 hours)
    */
    int accuracy = period/(target/10000);

    #ifdef ENABLE_LOG
      logMsg.concat("&RefPrev="); logMsg.concat(formatTOD(refPrev,1));
      logMsg.concat("&Period="); logMsg.concat(period); // logMsg.concat(": ");
      logMsg.concat("&Target="); logMsg.concat(target);
      logMsg.concat("&TargetX="); logMsg.concat(targetx);
      logMsg.concat("&Accuracy="); logMsg.concat(accuracy);
      // logMsg.concat(accuracy/100); logMsg.concat("."); //whole number
      // logMsg.concat((accuracy%100)/10); //tenths
      // logMsg.concat(accuracy%10); //hundredths
    #endif
    // #ifdef ENABLE_EINK
    //   //don't display refPrev
    //   display.setFont(&FreeSans12pt7b);
    //   displayY += (6+12)*1.5; display.setCursor(0, displayY);
    //   display.print("Period ");
    //   display.setFont(&FreeSansBold12pt7b);
    //   display.print(accuracy/100,DEC);
    //   display.print(F("."));
    //   display.print((accuracy%100)/10); //tenths
    //   display.print((accuracy%10)); //hundredths
    //   display.print(F("% "));
    //   display.print(targetx,DEC);
    //   display.print(F("x"));
    // #endif

    if(accuracy<9900 || accuracy>10100) {
      #ifdef ENABLE_LOG
        logMsg.concat("&Msg=Out of range; ignoring.");
      #endif

      #ifdef ENABLE_EINK
        display.setFont(&FreeSans12pt7b);
        displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
        display.print("Out of range.");

        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.print("Ignoring trigger.");

        display.display();
      #endif

      finish(logMsg);
    }

    //TODO remove the adjoffset

    //Calculate rate.
    //Rate should be per hour, so we grow or shrink period to be relative to a target of 3600000.

    //Examples:
    //Target is 60 minutes (3600000 ms)
    //Period is 72 minutes (4320000 ms)
    //0-(3600000-((3600000^2)/ 4320000 ))
    //Rate is -600000 ms/h (-10 min/hr)

    //Period is 62 minutes (3720000 ms)
    //Rate is -116129 ms/h (-1.94 min/hr)
    //a bit less than 2 mins, since the real hour hits first

    //Period is 57 minutes (3420000 ms)
    //Rate is +189473 ms/h (+3.15 min/hr)
    //a bit more than 3 mins, since the clock hour hits first

    //Target is 120 minutes (7200000 ms)
    //Period is 119 minutes (7140000 ms)
    //0-(3600000-((3600000^2)/ (7140000/(7200000/3600000)) ))
    //Rate is +30252 ms/h (+0.50 sec/hr)

    long periodPerHour = ((long long)period * 3600000)/target;

    long rate = 0-3600000+(12960000000000/periodPerHour);
    #ifdef ENABLE_LOG
      logMsg.concat("&Rate="); logMsg.concat(rate);
    #endif

    long adjReg = 0;

    if(triggerCount==2) {
      //second wake - we don't yet have ratePrev or adjRateFactor
      //Make arbitrary adjustment

      adjReg = (rate<=0? MOTOR_STEPS: 0-MOTOR_STEPS);
      #ifdef ENABLE_LOG
        logMsg.concat("&AdjRegTarget="); logMsg.concat(adjReg);
      #endif
      adjReg = moveMotor(adjReg); //it may max out before that
      #ifdef ENABLE_LOG
        logMsg.concat("&AdjRegActual="); logMsg.concat(adjReg);
        logMsg.concat("&Msg=At next rate, we will know change.");
      #endif

      #ifdef ENABLE_EINK
        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.setFont(&FreeSans12pt7b);
        display.print("Rate ");
        display.setFont(&FreeSansBold12pt7b);
        display.print(formatMils(rate,2));

        displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
        display.setFont(&FreeSans12pt7b);
        display.print("Adj ");
        display.setFont(&FreeSansBold12pt7b);
        if(adjReg>=0) display.print("+"); display.print(adjReg);

        display.setFont(&FreeSans9pt7b);
        display.print(" (test)");

        display.setFont(&FreeSans12pt7b);
        displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
        display.print("At next wake,");

        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.print("we'll know change.");
      #endif

    } else { //third+ wake - we have ratePrev so we can determine adjRateFactor from adjPrev

      int rateChg = rate - ratePrev;

      //TODO at first we'll set only one adjRateFactor, but we should move to averaging it
      if(triggerCount==3) {
        //This will set adjRateFactor to a positive value, expanded to if adjPrev was MOTOR_STEPS (it may have been less)
        adjRateFactor = abs(rateChg * (MOTOR_STEPS/adjRegPrev));
        // #ifdef SHOW_SERIAL
        //   Serial.print("Adj factor: ");
        //   Serial.println(adjRateFactor,DEC);
        // #endif
        #ifdef ENABLE_LOG
          logMsg.concat("&AdjFactor="); logMsg.concat(adjRateFactor);
        #endif
      }

      #ifdef ENABLE_EINK
        // display.setFont(&FreeSans12pt7b);
        // displayY += (6+12)*1.5; display.setCursor(0, displayY);
        // display.print("Rate' ");
        // display.setFont(&FreeSansBold12pt7b);
        // display.print(formatMils(ratePrev,2));
        
        display.setFont(&FreeSans12pt7b);
        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.print("Adj' ");
        display.setFont(&FreeSansBold12pt7b);
        if(adjRegPrev>=0) display.print("+"); display.print(adjRegPrev);

        display.setFont(&FreeSans12pt7b);
        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.print("Rate ");
        display.setFont(&FreeSansBold12pt7b);
        display.print(formatMils(rate,2));

        display.setFont(&FreeSans12pt7b);
        displayY += (6+12)*1.5; display.setCursor(0, displayY);
        display.print("Chg ");
        display.setFont(&FreeSansBold12pt7b);
        display.print(formatMils(rateChg,2));
      #endif

      

      //If there is an adjOffset, reverse it TODO

      //we finally do the magic - apply a regulation adj that is opposite of current rate
      adjReg = MOTOR_STEPS*((0-rate)/adjRateFactor);
      #ifdef ENABLE_LOG
        logMsg.concat("&AdjRegTarget="); logMsg.concat(adjReg);
      #endif
      adjReg = moveMotor(adjReg); //it may max out before that
      #ifdef ENABLE_LOG
        logMsg.concat("&AdjRegActual="); logMsg.concat(adjReg);
      #endif

      #ifdef ENABLE_EINK
        displayY += (6+6+12)*1.5; display.setCursor(0, displayY);
        display.setFont(&FreeSans12pt7b);
        display.print("Adj ");
        display.setFont(&FreeSansBold12pt7b);
        if(adjReg>=0) display.print("+"); display.print(adjReg);

        display.setFont(&FreeSans9pt7b);
        display.print(" (");
        display.print(formatMils(adjRateFactor,2));
        display.print(")");
      #endif

      //TODO display targeted time at correction?

    } //end third+ wake

    adjRegPrev = adjReg;
    ratePrev = rate;

  } //end second+ wake

  refPrev = ref;

  #ifdef SHOW_SERIAL
    Serial.println(logMsg);
  #endif

  display.display();


  /*
  * sample
      * Last = now (display)
  * sample
      * Prev = last, Last = now (display)
      * Last rate = prev vs last (display)
      * Assume rate factor 0 (display)
      * Last adj per arbitrary (display)
  * sample
      * Prev = last, Last = now (display)
      * Prev rate = last rate, Last rate = prev vs last (display)
      * Last ∆ rate = prev rate vs last rate (display)
      * rf0 = last ∆ rate vs last adj (display)
      * Last adj per last rate (display)
      * Last offadj per offset (display)
          * Display intended time
  * sample
      * Prev = last, Last = now (display)
      * Prev += offset (as though there were no offset) - remove offset
      * The rest is the same except last adj is an average of the last three rfs
  */

  //Once setup is done, finish up
  finish(logMsg);
    
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
        // Serial.print("You entered string ");
        // Serial.println(readString);
        if(readString=="w") inputStage=30; //stay awake
        if(readString=="c") inputStage=2; //enter clock setting
        if(readString=="m") inputStage=10; //enter motor setting
        if(readString=="a") inputStage=20;
        if(readString=="s") {
          logMsg.concat("&Msg=Commanded to sleep.");
          finish(logMsg);
        }

        //set RTC clock and move motor
        int incomingInt = readString.toInt();
        DateTime tod;
        switch(inputStage) {

          case 1: //w - keep it from sleeping
            break;

          case 2: //c - start setting clock
            Serial.println("Enter hour:");
            inputStage++;
            break;
          case 3:
            rtc.adjust(DateTime(2025,6,12,incomingInt,0,0));
            Serial.println("Enter minute:");
            inputStage++;
            break;
          case 4:
            tod = rtc.now();
            rtc.adjust(DateTime(2025,6,12,tod.hour(),incomingInt,0));
            Serial.println("Enter second:");
            inputStage++;
            break;
          case 5:
            tod = rtc.now();
            rtc.adjust(DateTime(2025,6,12,tod.hour(),tod.minute(),incomingInt));
            Serial.print("Clock set to: ");
            tod = rtc.now();
            Serial.print(tod.hour()%10,DEC); //hour tens
            Serial.print(tod.hour()/10,DEC); //hour ones
            Serial.print(":");
            Serial.print(tod.minute()%10,DEC); //min tens
            Serial.print(tod.minute()/10,DEC); //min ones
            Serial.print(":");
            Serial.print(tod.minute()%10,DEC); //sec tens
            Serial.print(tod.minute()/10,DEC); //sec ones
            Serial.println();

            #ifdef ENABLE_EINK
              display.setTextColor(EPD_BLACK);
              display.setFont(&FreeSans12pt7b);
              displayY += 12*1.5; display.setCursor(0, displayY);
              display.print("Clock set to: ");
              displayY += (6+12)*1.5; display.setCursor(0, displayY);
              display.setFont(&FreeSansBold12pt7b);
              if(tod.hour()<10) display.print("0");
              display.print(tod.hour());
              display.print(":");
              if(tod.minute()<10) display.print("0");
              display.print(tod.minute());
              display.print(":");
              if(tod.second()<10) display.print("0");
              display.print(tod.second());
              display.display();
              //TODOTODO
            #endif

            inputStage=99;
            break;

          case 10: //m - arbitrarily move motor
            Serial.println(F("Enter steps to move motor."));
            Serial.print(F("Current position: "));
            Serial.println(motorCur,DEC);
            inputStage++;
            break;
          case 11:
            if(incomingInt!=0) {
              //TODO could replace with moveMotor, but don't want it arbitrarily limited
              #ifdef ENABLE_MOTOR
                stepper.step(incomingInt);
              #endif
              motorCur+=incomingInt;
              Serial.print(F("Current position: "));
              Serial.println(motorCur,DEC);
            } else {
              inputStage=99;
            }
            break;
          
          case 20: //a - autocalibrate motor
            resetMotor(); //blocking
            inputStage=99;
            break;

          case 30: //w - stay awake
            Serial.println(F("Staying awake."));
            inputStage=1;
            break;
          
          case 99: //the end
            Serial.println(F("Done. Enter 's' to sleep, or other options."));
            inputStage=1;
            break;

          default: break;
        } //end switch inputStage
      } //end if serial available
    #endif
  #endif

  if(inputStage==0 && (millis()-millisStart>COLD_BOOT_SLEEP_PERIOD)) {
    logMsg.concat("&Msg=Sleep naturally.");
    finish(logMsg);
  }

}

void finish(String logMsg) {
  #ifdef SHOW_SERIAL
    Serial.println(logMsg);
  #endif
  #ifdef ENABLE_WIFI
    //Write to online database
    if(WiFi.status()==WL_CONNECTED){
      HTTPClient http;
      int httpReturnCode;
      for(int attempts=0; attempts<3; attempts++) {
        #ifdef SHOW_SERIAL
          Serial.print(F("Sending to log, attempt "));
          Serial.println(attempts,DEC);
        #endif
        unsigned long offset = millis()-millisStart;
        // http.begin(String(LOG_URL)+"&offset="+String(offset));
        http.begin(String(LOG_URL));
        // http.addHeader("Content-Type", "Content-Type: application/json"); //TODO? https://stackoverflow.com/a/60343909
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        httpReturnCode = http.POST(logMsg);
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
        #ifdef ENABLE_EINK
          display.setTextColor(EPD_RED);
          display.setFont(&FreeSans12pt7b);
          displayY += (12)*1.5; display.setCursor(0, displayY);
          display.print("Logging failed.");
        #endif
      }
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  #endif
  #ifdef SHOW_SERIAL
    Serial.flush();
  #endif
  //Go to sleep
  esp_deep_sleep_start();
}

void resetMotor() {
  #ifdef ENABLE_MOTOR
    //Based on fixed known range of regulator motor, it will send itself all the way down, stall a while, calibrate this as 0, then rise to center
    inputStage=20;
    #ifdef SHOW_SERIAL
      Serial.print(F("Moving motor to "));
      Serial.println(0-((MOTOR_MAX*102)/100),DEC);
    #endif
    //Can't use moveMotor here bc it won't allow a negative movement
    stepper.step(0-((MOTOR_MAX*102)/100));
    motorCur = 0;
    #ifdef SHOW_SERIAL
      Serial.print(F("Zeroed. Now centering at "));
      Serial.println(MOTOR_MAX/2,DEC);
    #endif
    moveMotor(MOTOR_MAX/2);
  #endif
}

long moveMotor(long motorChange) {
  #ifdef ENABLE_MOTOR
    //uses motorCur
    if(motorChange>0) {
      //don't move any further than MOTOR_MAX
      //if we're at 95, and max is 100, we can't go more than 5
      if(motorChange+motorCur > MOTOR_MAX) motorChange = MOTOR_MAX-motorCur;
      stepper.step(motorChange);
      motorCur+=motorChange;
      return motorChange;
    } else if(motorChange<0) {
      //don't move any further than 0 + MOTOR_NEG_OVERDRIVE
      //if it's at 35, and overdrive is 20, we can't go more than -15, to leave room for the overdrive
      if(motorCur+motorChange-MOTOR_NEG_OVERDRIVE < 0) motorChange = 0-motorCur;
      stepper.step(motorChange-MOTOR_NEG_OVERDRIVE); //overdrive sends it a little too far down...
      stepper.step(MOTOR_NEG_OVERDRIVE); //then back up, to ensure each adj ends on an up movement
      motorCur+=motorChange; //negative
      return motorChange;
    } else return 0;
  #endif
}

String formatTOD(unsigned long tod, byte decPlaces) {
  String todStr;
  todStr.concat((tod/3600000)/10); //hour tens
  todStr.concat((tod/3600000)%10); //hour ones
  todStr.concat(":");
  todStr.concat(((tod/60000)%60)/10); //min tens
  todStr.concat(((tod/60000)%60)%10); //min ones
  todStr.concat(":");
  todStr.concat(((tod/1000)%60)/10); //sec tens
  todStr.concat(((tod/1000)%60)%10); //sec ones
  if(decPlaces>0) {
    todStr.concat(".");
    todStr.concat((tod%1000)/100); //tenths
    if(decPlaces>1) {
      todStr.concat((tod%100)/10); //hundredths
      if(decPlaces>2) {
        todStr.concat((tod%10)); //hundredths
      }
    }
  }
  return todStr;
}

String formatMils(long mils, byte decPlaces) {
  String numStr;
  if(mils>=0) numStr.concat("+");
  else numStr.concat("-");
  numStr.concat(abs(mils/1000));
  if(decPlaces>0) {
    numStr.concat(".");
    numStr.concat(abs((mils%1000)/100)); //tenths
    if(decPlaces>1) {
      numStr.concat(abs((mils%100)/10)); //hundredths
      if(decPlaces>2) {
        numStr.concat(abs(mils%10)); //thousandths
      }
    }
  }
  return numStr;
}