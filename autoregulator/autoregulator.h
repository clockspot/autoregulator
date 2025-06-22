#ifndef AUTOREGULATOR_H
#define AUTOREGULATOR_H

//Hardware config to use
#include "configs/lm-ericsson.h"

//Declarations
void setup();
void loop();
void goToSleep();
long moveMotor(long motorChange);
void resetMotor();
void displayPrintTime(unsigned long tod, byte milPlaces);

#endif //AUTOREGULATOR_H