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
#define MOTOR_STEPS 20
#define MOTOR_SPEED 60
#define MOTOR_A GPIO_NUM_18
#define MOTOR_B GPIO_NUM_17
#define MOTOR_C GPIO_NUM_9
#define MOTOR_D GPIO_NUM_8
#define MOTOR_MAX 1400
//For the DFRobot FIT0708, the max is 1600 without anything attached. Remove 50 for every mm of clearance you need back (e.g. a 3D-printed bracket attached to the bolt), plus maybe an extra 50 margin.
//To find the max for another motor, restart the Autoregulator with Serial connected, and enter 'm'. This will enter an interactive session where you can move the motor up and down, displaying the position relative to its current 'zero'. Move it as far down as it will go (until it binds), then move it up until it almost reaches the max (but don't let it bind). Subtract the lowest value from the highest value to find the max.
//TODO interactive discovery mode using BOOT button
#define MOTOR_NEG_OVERDRIVE 10
//To help ensure the weight is always positioned on top of a thread.

// #define PERIOD_MILS 60000 //once per minute
#define PERIOD_MILS 3600000 //once per hour

#define COLD_BOOT_SLEEP_PERIOD 30000

#define ENABLE_DS3231
#define Wire Wire1 //to use DS3231 on STEMMA QT

//#define ENABLE_NTP_SYNC

#define NETWORK_SSID "SSID"
#define NETWORK_PASS "PASSWORD"

#define LOG_URL "https://website/?auth=AUTHKEY&table=TABLE"

#define NTP_HOST "pool.ntp.org"
//#define TZ_OFFSET_SEC -21600 //will go at midnight in this time zone
#define TZ_OFFSET_SEC -18000 //will go at midnight in this time zone - 1am Central
#define DST_OFFSET_SEC 3600

#endif //CONFIG_H