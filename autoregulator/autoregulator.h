#ifndef AUTOREGULATOR_H
#define AUTOREGULATOR_H

//Hardware config to use
#include "configs/lm-ericsson.h"

//Declarations
void setup();
void loop();
void finish(String logMsg);
long moveMotor(long motorChange);
void resetMotor();
String formatTOD(unsigned long tod, byte decPlaces);

#endif //AUTOREGULATOR_H