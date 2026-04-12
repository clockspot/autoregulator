#ifndef CONFIG_H
#define CONFIG_H

#define SHOW_SERIAL
#define ENABLE_NEOPIXEL

//Pins for the Adafruit ESP32-S2 QT Py
// https://learn.adafruit.com/adafruit-qt-py-esp32-s2/pinouts

//Desc  GPIO  Intent
//A0    18    motor
//A1    17    motor
//A2    9     motor
//A3    8     motor
//SDA   7     ECS (e-ink controller select)
//SCL   6     DC (data/control)
//TX    5     interrupt from clock

//MO    35    MOSI
//MI    37    BUSY
//SCK   36    SCK
//RX    16    ENABLE

//STEMMA QT port:
//SDA1  41    RTC
//SCL1  40    RTC

// #define BATTERY_MONITOR_PIN GPIO_NUM_9 //A2, when QT Py is equipped with LiPo BFF
#define WAKEUP_PIN GPIO_NUM_5 //MI not needed to drive e-ink display

#define ENABLE_EINK
#define EPD_DC GPIO_NUM_6
#define EPD_CS GPIO_NUM_7
#define EPD_BUSY GPIO_NUM_37 // can set to -1 to not use a pin (will wait a fixed delay)
#define SRAM_CS -1
#define EPD_RESET -1 // can set to -1 and share with microcontroller Reset!
#define EPD_SPI &SPI // primary SPI
#define EINK_ROTATION 0

#define ENABLE_MOTOR
#define MOTOR_STEPS 40 //For the DFRobot FIT0708, a complete rev is 20, but 40 will give a better adj factor.
#define MOTOR_SPEED 30
#define MOTOR_A GPIO_NUM_18
#define MOTOR_B GPIO_NUM_17
#define MOTOR_C GPIO_NUM_9
#define MOTOR_D GPIO_NUM_8

//To help ensure the weight is always positioned on top of a thread.
#define MOTOR_NEG_OVERDRIVE 10

//ms/hr change in clock rate per MOTOR_STEPS of adjustment. Tune to your clock:
//start conservatively high (under-corrects but won't overshoot), then decrease if convergence is slow.
#define ADJ_FACTOR 220 //derived from observed data (~209-233 over first 3 wakes)

//Known total travel range of this motor in steps. For alerting when max has been reached.
#define MOTOR_RANGE 1500

#define PERIOD_MILS 3600000 //once per hour

#define ENABLE_SYNC //apply a temporary offset adjustment each trigger to phase-align with reference time

#define COLD_BOOT_SLEEP_PERIOD 30000

//Either a DS3231 RTC and/or WiFi+NTP sync must be enabled.
// #define ENABLE_DS3231
// #define Wire Wire1 //to use DS3231 on STEMMA QT

// #define ENABLE_NTP_SYNC

#define ENABLE_WIFI
#define WIFI_SSID "SSID"
#define WIFI_PASS "PASSWORD"

#define LOG_URL "https://website/?auth=AUTHKEY&table=TABLE"

#define NTP_HOST "pool.ntp.org"
#define NTP_HOST2 "time.nist.gov"
#define TIME_ZONE "EST5EDT,M3.2.0,M11.1.0" //TZ_US_Eastern
//Find your time zone string in https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h

#endif //CONFIG_H