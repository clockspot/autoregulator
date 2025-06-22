# autoregulator

This will be a small battery-powered device that automatically regulates a pendulum clock and syncs it to NTP via WiFi. The code runs on an ESP32 that monitors the clock's timekeeping and applies corrections via a micro servo motor attached to a threaded screw on the pendulum rod.

At this writing, it simply sleeps until woken by a switch attached to the clock, then sends out an HTTP request for logging/dev purposes.

## Hardware

Prototype implementation uses hardware selections from Adafruit: [QT Py ESP32-S2](https://www.adafruit.com/product/5325) with [BFF power supply/charger](https://www.adafruit.com/product/5397) and [battery](https://www.adafruit.com/product/1781). The QT Py connections are:

* A2 (GPIO 9) to BFF if applicable (and enable `BATTERY_MONITOR_PIN` in config)
* A3 (GPIO 8) to RTC SQW (this is the interrupt pin the RTC uses to wake up the ESP32)

## Pseudocode

* Woken: capture millis1
- If esp is woken cold:
  * Set ref = rate = adjfactor = rateadj = offsetadj = null
  * Wait for a new sketch, then end
- If esp is woken by interrupt:
  * Capture millis2
  * Grab reference time
  * Capture millis3
  * ref -= (millis2-millis1)+((millis3-millis2)/2) - our best guess at ref time when interrupt occurred

  - If refLast:


    - If adjOffset:

      * Apply adj: -adjOffset
      * adjOffset = 0
    
    * period = ref - (refLast ± offsetCorrectionLast)





  - If refLast is set (this is the 2nd+ sample):

    * period = ref - refLast

    * rate = -60+((60/period)*60)
      e.g. 3:05-2:03 = 185-123 = 62: -60+((60/62)*60) = -1.94
      "60min" in 62min = "58.06min" in 60min = loss of 1.94min/h
      in other words, at 3:03, it was 1.94min behind - reading 3:01 and a bit

    - If rateLast is not set: (this is the 2nd sample)

      * Make arbitrary adjustment:
        adjRate = ±1

    - Else (this is the 3rd+ sample)

      * rateChg = rate - rateLast
      
      - If adjFactor is not set: (this is the 3rd sample)

        * Set adjFactor = rate - rateLast
      
      * If there is an adjOffset, reverse it
      * [If the rate didn't get corrected as expected, reset adjFactor and redo?]
      Here there will be an adjRate and possibly an adjOffset

    


    * Set rate-1 = rate
    * Set rateadj-1 = rateadj
    * Set offsetadj-1 = offsetadj

  * Set ref-1 = ref


## Tests

Sample 1 taken at h:m:s
Awaiting next sample

Sample 2 taken at h:m:s
Period: m:s  Rate: ±s.s/h
Adj made: ±5

Sample 3 taken at h:m:s
Period: m:s   Prev period: m:s
Rate: ±s.s/h   Prev rate: ±s.s/h
Rate chg: x  Adj: x  Adj effect: x/h
Adj made: ±x (rate) ±x (±m:s offset)
Next sample expected at ~h:m:s



The period may differ from the previous period per two adjustments -
one to correct the rate, and one to correct the offset.
The offset correction can be reversed at this point, and period should discount it





Time      Rate      Sync      Next
06:54:00  —         —         —
07:54:00  0         +...      —
08:55:00  (Ok, we found out the adj factor. We need to gain 5 mins in the next hour)
          0         –... +180s/h (this is all we could do)
                              09:58:00
09:58:00  0         –180s/h +120s/h
11:00:01  (We gained one more second than we intended to, so that's the rate)
          -1s/h     –120s/h -1s/h
12:00:00  0         +1s/h 0
13:00:00  0         0
14:00:01  -1s/h     -1s/h
14:59:59  +1s/h     +1s/h

Rate is a permanent adjustment to correct the speed.
Sync is a temporary adjustment to correct the offset from reference time. It is reversed at the next sample.

Time      Rate      Sync      Next
06:54:00  —         —         —
07:53:00  (is neg)  +...      —
08:53:00  +60s/h    –... +180s/h
                              09:57:00
09:57:00  0         –180s/h +180s/h
                              11:00:00
11:00:00  0         –180s/h

Time      Rate      Sync      Next
06:54:00  —         —         —
07:53:00  +[60s/h]  (is beh)  —
08:53:00  0         +240s/h
                              09:57:00
09:57:00  0         –180s/h +180s/h
                              11:00:00
11:00:00  0         –180s/h

Now how do we determine rate over average samples due to bugs etc


Time      Rate      Adj       Offset    Adj*      Next
06:54:00  —         —         –360s     —         —
07:53:00  -60s/h    +...      —420s     —         —
08:53:00  0         —         –420s     +180s/h   09:57:00
09:57:01  +1s/h     –1s/h     –179s/h   +179s/h   11:00:00
11:00:00  0         –180s/h

Time      Rate      Adj       Offset    Adj*      Next
06:54:00  —         —         –360s     —         —
07:52:00  -120s/h   +...      —420s     —         —
08:51:00  –60s/h    +60s/h    –540s     +180s/h   09:54:00
09:54:02  +2s/h     —2s/h     –360s     +180s/h   10:57:00
10:57:01  +1s/h     –1s/h     –179s/h   +179s/h   12:00:00
12:00:00  0         —

Sample 1 (0h):
  take ref time.
  (determine offset.)

Sample 2 (1h):
  take ref time.
  (determine offset.)
  determine rate compared to previous ref.
  make arbitrary adjustment to correct either rate or offset (or both?)

Sample 3 (2h):
  take ref time.
  (determine offset.)
  determine rate.
  determine adj effect compared to previous ref.
  make adjustments for both rate and offset.


Each time it is triggered, the autoregulator obtains a reference time (NTP or quartz). Once at least two reference times are obtained:

- It makes a permanent adjustment to correct the clock's rate (ref vs. previous ref).
- It makes a temporary adjustment to correct the clock's offset from the reference time. If the previous trigger made such an adjustment, it reverses it.

The first adjustment (after the second trigger) is arbitrary, used to determine the adjustment factor. 


About the Host Clock

This Ericsson master clock uses a Swiss Moser-Baer movement that generates alternating-polarity impulses with a pair of double-throw mercury switches, used to wind the clock and drive a secondary clock circuit. It's also equipped with a bell circuit, which is used here to trigger the Autoregulator every hour. The dial and pendulum are custom-fabricated, and the paper condenser has been disconnected in favor of a modern ceramic capacitor; otherwise it remains original.