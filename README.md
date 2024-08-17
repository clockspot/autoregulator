# autoregulator

This will be a small battery-powered device that automatically regulates a pendulum clock and syncs it to NTP via WiFi. The code runs on an ESP32 that monitors the clock's timekeeping and applies corrections via a micro servo motor attached to a threaded screw on the pendulum rod.

At this writing, it simply sleeps until woken by a switch attached to the clock, then sends out an HTTP request for logging/dev purposes.

## Hardware

Prototype implementation uses hardware selections from Adafruit: [QT Py ESP32-S2](https://www.adafruit.com/product/5325) with [BFF power supply/charger](https://www.adafruit.com/product/5397) and [battery](https://www.adafruit.com/product/1781). The QT Py connections are:

* A2 (GPIO 9) to BFF if applicable (and enable `BATTERY_MONITOR_PIN` in config)
* A3 (GPIO 8) to RTC SQW (this is the interrupt pin the RTC uses to wake up the ESP32)