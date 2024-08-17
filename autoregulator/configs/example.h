#ifndef CONFIG_H
#define CONFIG_H

//#define SHOW_SERIAL
#define ENABLE_NEOPIXEL

//pins expressed in gpio_num_t format
#define BATTERY_MONITOR_PIN GPIO_NUM_9 //A2, when QT Py is equipped with LiPo BFF
#define WAKEUP_PIN GPIO_NUM_8 //A3 per https://learn.adafruit.com/adafruit-qt-py-esp32-s2/pinouts

//#define ENABLE_NTP_SYNC

#define NETWORK_SSID "SSID"
#define NETWORK_PASS "PASSWORD"

#define LOG_URL "https://website/?auth=AUTHKEY&table=TABLE"

#define NTP_HOST "pool.ntp.org"
//#define TZ_OFFSET_SEC -21600 //will go at midnight in this time zone
#define TZ_OFFSET_SEC -18000 //will go at midnight in this time zone - 1am Central
#define DST_OFFSET_SEC 3600

#endif //CONFIG_H