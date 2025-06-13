#ifndef AUTOREGULATOR_H
#define AUTOREGULATOR_H

//Hardware config to use
#include "configs/lm-ericsson.h"

//Declarations
void setup();
void loop();
void goToSleep();
void moveMotor(bool dir);
void resetMotor();

#endif //AUTOREGULATOR_H