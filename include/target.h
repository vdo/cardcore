// What MeshCore's helpers expect a board variant to provide (defined in src/main.cpp)
#pragma once
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/SensorManager.h>

extern CustomSX1262Wrapper radio_driver;
extern SensorManager sensors;  // none: the GPS is read directly
