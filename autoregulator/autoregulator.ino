// Low-power ESP32 solution to autoregulate pendulum clocks per NTP, via a stepper motor to adjust pendulum
// https://github.com/clockspot/autoregulator
// Sketch by Luke McKenzie (luke@theclockspot.com)

#include <arduino.h>
#include "autoregulator.h" //specifies config
#include "esp_sleep.h"
#include "esp_sntp.h"


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
  #define TEXT_LINE_HT 27
  #define TEXT_LINE_LD 8 //leading between lines for breaks
  int displayY = 0 - TEXT_LINE_LD; //start a little off the top of the screen
#endif

#ifdef ENABLE_MOTOR
  #include <Stepper.h>
  Stepper stepper(MOTOR_STEPS, MOTOR_A, MOTOR_B, MOTOR_C, MOTOR_D);
#endif

#ifdef ENABLE_DS3231
  #include <RTClib.h>
  RTC_DS3231 rtc;
  // TODO get temp from DS3231
#endif

#ifdef ENABLE_WIFI
  #include <WiFi.h>
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
bool wakeValid;

RTC_DATA_ATTR unsigned long triggerCount = 0;
  //using unsigned long because, why not, if we have the space? Allows for up to 4,294,967,295 samples, which is enough for hourly samples for 500,000 years. There are only 8760 hourly samples in a year so could reasonably go with word (65535) if needed.
RTC_DATA_ATTR unsigned long refPrev = 0; //Reference time at most recent sample - millis per day (86400000). Known by trigger 2.
RTC_DATA_ATTR int motorPos = 0; //Cumulative motor position in steps from initial center position
RTC_DATA_ATTR int adjOffPrev = 0; //sync offset applied at last trigger (motor steps); always reversed at next trigger
RTC_DATA_ATTR bool failState = 0; //if we need some human intervention, and need to stop doing things until rebooted

unsigned long ref = 0; //We will populate this with a reference time, either from RTC or NTP, and backdate it by the time it took to get it (100% of the time to when we start the request, and in the case of NTP, 50% of the time it takes to get the request back), so this will represent as accurately as possible the moment when the clock triggered it

void setup() {

  millisStart = millis();

  delay(300); //solves a bug of some kind - TODO does it need to be this big?
  //https://www.instructables.com/ESP32-Deep-Sleep-Tutorial/
  //https://simplyexplained.com/courses/programming-esp32-with-arduino/using-rtc-memory/

  if(failState) goToSleep();

  //Verify the pin is still LOW after a settling delay.
  //A real switch closure holds LOW for many milliseconds; a noise glitch does not.
  if(digitalRead(WAKEUP_PIN) == LOW) wakeValid = true;

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
    if(ref>86399999) ref+=86400000; //just after midnight, when millis()-millisStart > time of day in millis, ref will be negative (rollover) and need to be fixed. Otherwise you get a ref like 4294966293 1193:02:46.2 (per uint32_t rollover after 4294967295).
  #endif

  #ifdef ENABLE_EINK
    display.begin(THINKINK_TRICOLOR);
    display.setRotation(EINK_ROTATION);
    display.clearBuffer();
    //the display contents will be built up procedurally as we go, like serial output
    display.setTextColor(EPD_BLACK);
    display.setFont(&FreeSans12pt7b);
  #endif

  //TODO If the motor is not enabled, it will just pretend to drive one and display the results
  #ifdef ENABLE_MOTOR
    stepper.setSpeed(60);
  #endif

  #ifdef ENABLE_NTP_SYNC
    bool ntpOk = false;
    int ntpFails = 0;
  #endif

  #ifdef ENABLE_WIFI
    #ifdef SHOW_SERIAL
      Serial.print(F("Connecting to WiFi SSID "));
      Serial.print(WIFI_SSID);
      Serial.println(F("..."));
    #endif
    WiFi.mode(WIFI_STA);
    int wifiFails;
    for(wifiFails=0; wifiFails<3; wifiFails++) {
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
          Serial.print(F("WiFi connected after "));
          Serial.print(wifiFails);
          Serial.println(F("fails."));
          //Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
          Serial.print(F("Signal strength (RSSI): ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
          Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());
        #endif
        //don't display anything on the e-ink

        #ifdef ENABLE_NTP_SYNC
          #ifdef SHOW_SERIAL
            Serial.print(F("Syncing to NTP..."));
          #endif
          //configTzTime starts the SNTP process (non-blocking); call it once.
          //POSIX TZ string handles DST automatically.
          #ifdef NTP_HOST2
            configTzTime(TIME_ZONE, NTP_HOST, NTP_HOST2);
          #else
            configTzTime(TIME_ZONE, NTP_HOST);
          #endif
          //getLocalTime() blocks for up to the given timeout waiting for sync.
          //Retry a few times in case the first window is too short.
          struct tm timeinfo;
          for(ntpFails=0; ntpFails<3 && !ntpOk; ntpFails++) {
            ntpOk = getLocalTime(&timeinfo, 5000);
          }
          if(ntpOk) {
            #ifdef SHOW_SERIAL
              Serial.print(F("NTP success after "));
              Serial.print(ntpFails);
              Serial.println(F(" fails."));
            #endif
            //Snapshot millis() and gettimeofday() back-to-back so tv_usec gives the exact
            //sub-second offset with no second-boundary race.
            unsigned long millisAtTV = millis();
            struct timeval tv;
            gettimeofday(&tv, NULL);
            //Re-derive broken-down time from tv.tv_sec (already TZ-adjusted by configTzTime)
            //so the seconds used for RTC and for ref are consistent with tv_usec.
            struct tm *ti = localtime(&tv.tv_sec);
            #ifdef ENABLE_DS3231
              //Update RTC from NTP (tm_year is years since 1900; tm_mon is 0-based)
              rtc.adjust(DateTime(ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
                                  ti->tm_hour, ti->tm_min, ti->tm_sec));
            #endif
            #ifndef ENABLE_DS3231
              //No RTC - derive ref from NTP with sub-second precision, backdated to trigger
              unsigned long todNow = (unsigned long)ti->tm_hour * 3600000UL
                                   + (unsigned long)ti->tm_min  * 60000UL
                                   + (unsigned long)ti->tm_sec  * 1000UL
                                   + (unsigned long)(tv.tv_usec  / 1000);
              ref = 0UL - (millisAtTV - millisStart) + todNow;
              if(ref > 86399999UL) ref += 86400000UL;
            #endif
          } else {
            #ifdef SHOW_SERIAL
              Serial.println(F("NTP sync failed."));
            #endif
            #ifdef ENABLE_EINK
              display.setTextColor(EPD_RED);
              display.setCursor(0, displayY += TEXT_LINE_HT);
              display.print("NTP sync failed.");
              display.setTextColor(EPD_BLACK);
            #endif
          }
        #endif //end ntp sync

        break; //leave wifi attempt loop
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
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("WiFi failed.");
        display.setTextColor(EPD_BLACK);
      #endif
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  #endif

  //Start log string
  #ifdef ENABLE_LOG
    logMsg.concat("Ref=");
    logMsg.concat(formatTOD(ref,1));
    #ifdef ENABLE_NTP_SYNC
      logMsg.concat("&NTPOK=");
      logMsg.concat(ntpOk ? 1 : 0);
      logMsg.concat("&NTPFails=");
      logMsg.concat(ntpFails);
    #endif
  #endif

  //Who disturbs my slumber??
  if(esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_EXT0) { //Cold start
    //Does this make a difference vs. ==ESP_SLEEP_WAKEUP_UNDEFINED ?
    //TODO could also just be based on triggerCount=0
    
    #ifdef ENABLE_LOG
      logMsg.concat("&Wake=0");
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
    #endif
  
    #ifdef BATTERY_MONITOR_PIN
      delay(3000); //let battery level indicator show for a bit
    #endif

    #ifdef ENABLE_NEOPIXEL
      pixels.fill(0x00FFFF); //teal to indicate cold start wait
      pixels.show();
    #endif

    #ifdef ENABLE_EINK
      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.setFont(&FreeSansBold12pt7b);
      display.print("Autoregulator");
      display.setFont(&FreeSans12pt7b);

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("by @clockspot");

      displayY += TEXT_LINE_LD;

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("Time ");
      display.setFont(&FreeSansBold12pt7b);
      display.print(formatTOD(ref,1));
      display.setFont(&FreeSans12pt7b);

      displayY += TEXT_LINE_LD;

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("Awaiting trigger.");

      display.display();
    #endif

    return; //We'll let loop() monitor for serial inputs and leave time for a new sketch upload
    
  } //end cold start

  //otherwise we woke from sleep, probably by ESP_SLEEP_WAKEUP_EXT0
  //in this part, the log string is written to both serial and wifi log

  if(!wakeValid) {
    //Did we find the wakeup pin was still LOW after a settling delay?
    #ifdef ENABLE_LOG
      logMsg.concat("&Msg=Spurious");
      writeLog(logMsg);
    #endif
    goToSleep();
  }

  triggerCount++;

  #ifdef ENABLE_LOG
    logMsg.concat("&Wake=");
    logMsg.concat(triggerCount);
  #endif

  #ifdef ENABLE_EINK
    display.setCursor(0, displayY += TEXT_LINE_HT);
    display.print("Wake ");
    display.setFont(&FreeSansBold12pt7b);
    display.print(triggerCount);
    display.setFont(&FreeSans12pt7b);

    display.setCursor(0, displayY += TEXT_LINE_HT);
    display.print("Time ");
    display.setFont(&FreeSansBold12pt7b);
    display.print(formatTOD(ref,1));
    display.setFont(&FreeSans12pt7b);
  #endif

  if(triggerCount==1) {
    //first wake - no rate yet, awaiting second trigger
    #ifdef ENABLE_LOG
      logMsg.concat("&Msg=At next wake, we will know rate.");
    #endif
    #ifdef ENABLE_EINK
      displayY += TEXT_LINE_LD;

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("At next wake,");

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("we'll know rate.");
    #endif

  } else {
    //second+ wake - calculate rate and apply P controller

    //Reverse the previous sync offset, if applicable.
    //Capture the value first - it's needed below to compensate the rate calculation.
    int prevOff = adjOffPrev;
    moveMotor(-prevOff);
    adjOffPrev = 0;

    long period = ref - refPrev; //should be ~3600000
    if(period<0) period+=86400000; //midnight rollover

    // Tolerate missed samples by rounding period to the nearest whole multiple of PERIOD_MILS.
    // +(PERIOD_MILS/2) makes the integer division round rather than truncate.
    int targetx = (period+(PERIOD_MILS/2))/PERIOD_MILS;
    long target = PERIOD_MILS*targetx;
    // Discard if more than 1% out of target (percentage expressed to 4 decimal places)
    int accuracy = period/(target/10000);

    #ifdef ENABLE_LOG
      logMsg.concat("&RefPrev="); logMsg.concat(formatTOD(refPrev,1));
      logMsg.concat("&Period="); logMsg.concat(period);
      logMsg.concat("&Target="); logMsg.concat(target);
      logMsg.concat("&TargetX="); logMsg.concat(targetx);
      logMsg.concat("&Accuracy="); logMsg.concat(accuracy);
    #endif

    if(accuracy<9900 || accuracy>10100) {
      //Out of reasonable sample range: probably means we manually set the clock, so we need to start over.
      triggerCount = 0;
      refPrev = 0;
      adjOffPrev = 0;

      #ifdef ENABLE_LOG
        logMsg.concat("&Msg=Out of range. Starting over.");
      #endif

      #ifdef ENABLE_EINK
        displayY += TEXT_LINE_LD;

        display.setTextColor(EPD_RED);
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("Out of range.");
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("Starting over.");

        display.display();
      #endif

      writeLog(logMsg);
      goToSleep();
    }

    //Rate in ms/hr: positive = clock gaining (running fast), negative = clock losing (running slow).
    //Uses long long for intermediate to avoid overflow of 3600000^2.
    long periodPerHour = (long)((long long)period * 3600000LL / target);
    long rate = (long)(3600000LL * 3600000LL / (long long)periodPerHour) - 3600000L;

    #ifdef ENABLE_LOG
      logMsg.concat("&Rate="); logMsg.concat(rate);
    #endif

    #ifdef ENABLE_EINK
      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("Rate ");
      display.setFont(&FreeSansBold12pt7b);
      display.print(formatMils(rate,2));
      display.setFont(&FreeSans12pt7b);

      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("Pos ");
      display.setFont(&FreeSansBold12pt7b);
      if(motorPos>=0) display.print("+");
      display.print(motorPos);
      display.setFont(&FreeSans12pt7b);
    #endif

    //P controller: steps needed to null the rate.
    //Subtract prevOff's known contribution from measured rate so regulation targets the true
    //clock rate, not one artificially shifted by the previous sync offset.
    //Multiply before divide to preserve integer precision; use long to handle large rates before clamping.
    long rateForReg = rate - ((long)prevOff * ADJ_FACTOR / MOTOR_STEPS);
    long adjSteps = ((long long)(0 - rateForReg) * MOTOR_STEPS) / ADJ_FACTOR;

    //Clamp to motor position limits and apply
    long newPosL = (long)motorPos + adjSteps; //TODO why are we calculating this as a long then downconverting to int?
    int newPos = (int)newPosL;
    adjSteps = newPos - motorPos;

    moveMotor(adjSteps);
    motorPos = newPos;

    #ifdef ENABLE_LOG
      logMsg.concat("&Adj="); logMsg.concat(adjSteps);
      logMsg.concat("&MotorPos="); logMsg.concat(motorPos);
    #endif

    #ifdef ENABLE_EINK
      displayY += TEXT_LINE_LD;
        
      display.setCursor(0, displayY += TEXT_LINE_HT);
      display.print("Adj ");
      display.setFont(&FreeSansBold12pt7b);
      if(adjSteps>=0) display.print("+");
      display.print(adjSteps);
      display.setFont(&FreeSans12pt7b);
      display.print(" to ");
      display.setFont(&FreeSansBold12pt7b);
      display.print(motorPos);
      display.setFont(&FreeSans12pt7b);
    #endif

    #ifdef ENABLE_SYNC
      //Offset: signed ms between this trigger and the nearest period boundary.
      //Positive = triggered late = clock is slow; negative = triggered early = clock is fast.
      //Same sign convention as rate, so the formula below needs no inversion.
      long offset = (long)(ref % (unsigned long)PERIOD_MILS);
      if(offset > (long)PERIOD_MILS / 2) offset -= (long)PERIOD_MILS;

      //Sync adjustment uses the identical formula as regulation, targeting phase rather than rate.
      //Clamped to ±MOTOR_RANGE/10 to limit motor excursion from this temporary adjustment.
      long adjOff = ((long long)offset * MOTOR_STEPS) / ADJ_FACTOR;
      int maxOff = MOTOR_RANGE / 10;
      if(adjOff >  maxOff) adjOff =  maxOff;
      if(adjOff < -maxOff) adjOff = -maxOff;

      moveMotor((int)adjOff);
      adjOffPrev = (int)adjOff;

      #ifdef ENABLE_LOG
        logMsg.concat("&Offset="); logMsg.concat(offset);
        logMsg.concat("&AdjOff="); logMsg.concat(adjOff);
      #endif
      #ifdef ENABLE_EINK
        display.setTextColor(EPD_BLACK);
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("Sync ");
        display.setFont(&FreeSansBold12pt7b);
        display.print(formatMils(offset, 1));
        display.setFont(&FreeSans12pt7b);
        display.print(" / ");
        display.setFont(&FreeSansBold12pt7b);
        if(adjOff >= 0) display.print("+");
        display.print((int)adjOff);
        display.setFont(&FreeSans12pt7b);
      #endif
    #endif

    if(abs(motorPos) >= MOTOR_RANGE) {
      #ifdef ENABLE_LOG
        logMsg.concat("&Msg=Error: motor at limit. Fix and restart.");
      #endif
      #ifdef ENABLE_EINK
        displayY += TEXT_LINE_LD;
        
        display.setTextColor(EPD_RED);
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("Motor at limit.");
        display.setCursor(0, displayY += TEXT_LINE_HT);
        display.print("Fix and restart.");
      #endif
      failState = true;
    }

  } //end second+ wake

  refPrev = ref;

  #ifdef SHOW_SERIAL
    Serial.println(logMsg);
  #endif

  display.display();


  //Once setup is done, finish up
  writeLog(logMsg);
  goToSleep();
    
}


int inputStage = 0;
int incomingByte = 0;

void loop() {
  //Only runs on cold start, for monitoring for serial input

  #ifdef SHOW_SERIAL
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
      #ifdef ENABLE_DS3231
        if(readString=="c") inputStage=2; //enter clock setting
      #endif
      if(readString=="m") inputStage=10; //enter motor setting
      if(readString=="s") {
        logMsg.concat("&Msg=Start. Commanded to sleep.");
        writeLog(logMsg);
        goToSleep();
      }

      //set RTC clock and move motor
      int incomingInt = readString.toInt();
      #ifdef ENABLE_DS3231
        DateTime tod;
      #endif
      switch(inputStage) {

        case 1: //w - keep it from sleeping
          break;

        #ifdef ENABLE_DS3231
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
            display.setCursor(0, displayY += TEXT_LINE_HT);
            display.print("Clock set to: ");
            display.setCursor(0, displayY += TEXT_LINE_HT);
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
        #endif

        case 10: //m - arbitrarily move motor
          Serial.println(F("Enter steps to move motor."));
          inputStage++;
          break;
        case 11:
          if(incomingInt!=0) {
            //TODO could replace with moveMotor, but don't want it arbitrarily limited
            Serial.print(F("Moving motor by "));
            Serial.print(incomingInt,DEC);
            Serial.println(F("... "));
            #ifdef ENABLE_MOTOR
              stepper.step(incomingInt);
            #endif
            Serial.println(F("Done. Enter another value or 's' to sleep."));
          } else {
            inputStage=99;
          }
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

  if(inputStage==0 && (millis()-millisStart>COLD_BOOT_SLEEP_PERIOD)) {
    logMsg.concat("&Msg=Start. Sleep naturally.");
    writeLog(logMsg);
    goToSleep();
  }

}

void writeLog(String logMsg) {
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
          display.setCursor(0, displayY += TEXT_LINE_HT);
          display.print("Logging failed.");
        #endif
      }
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  #endif
}

void goToSleep() {
  #ifdef SHOW_SERIAL
    Serial.flush();
  #endif
  //Go to sleep
  esp_deep_sleep_start();
}

void moveMotor(long motorChange) {
  #ifdef ENABLE_MOTOR
    if(motorChange>0) {
      stepper.step(motorChange);
    } else if(motorChange<0) {
      stepper.step(motorChange-MOTOR_NEG_OVERDRIVE); //overdrive sends it a little too far down...
      stepper.step(MOTOR_NEG_OVERDRIVE); //then back up, to ensure each adj ends on an up movement
    }
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