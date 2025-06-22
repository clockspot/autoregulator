# autoregulator

The Autoregulator is an experimental device to automatically regulate and synchronize a pendulum clock. It monitors the clock’s timekeeping, compares it to a thermocompensated quartz time reference, calculates the clock’s rate, compensates by moving a small weight on a stepper motor attached to the pendulum rod, and shows the results on a small E-Ink display. It is based on an Adafruit ESP32 microcontroller and DS3231 real-time clock chip. A future version will add support for synchronization and the option of NTP over WiFi as the time reference.

More info at [theclockspot.com/autoregulator](https://theclockspot.com/autoregulator).

## Hardware prototype

* [Adafruit QT Py ESP32-S2 microcontroller](https://www.adafruit.com/product/5325)
* [Adafruit DC Power BFF](https://www.adafruit.com/product/5882) or [LiIon/LiPoly BFF](https://www.adafruit.com/product/5397)
* [Adafruit DS3231 Precision RTC](https://www.adafruit.com/product/5188)
* [Adafruit ThinkInk E-Ink display](https://www.adafruit.com/product/4868)
* [Adafruit 36AWG wire](https://www.adafruit.com/product/4733)
* [DFRobot FIT0708 stepper motor](https://www.mouser.com/ProductDetail/DFRobot/FIT0708?qs=DRkmTr78QARtq9KjV%252BGo3A%3D%3D)

I also used a 3D printer to make a bracket to mount weights to the motor (see `openscad` folder) and to mount the rest of the hardware to [my test clock](https://theclockspot.com/autoregulator).

## Software setup

* Duplicate `configs/example.h`, edit to suit, and specify your config file in `autoregulator.h`.
* If using the Arduino IDE (as I currently do):
  * Add support for ESP32 with [these instructions from Adafruit](https://learn.adafruit.com/adafruit-qt-py-esp32-s2/arduino-ide-setup)
  * Use the Library Manager to install Adafruit libraries for BusIO, NeoPixel, RTClib, and ThinkInk (and their dependencies).
  * Uses the standard Arduino library for Stepper. This is blocking code, but that's suitable for this purpose.