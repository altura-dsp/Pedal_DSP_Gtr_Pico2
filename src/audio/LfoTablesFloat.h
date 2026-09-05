#pragma once

#include <stdint.h>

#define LFO_TABLE_SIZE_FLOAT 1024

extern const float lfoSineTableFloat[LFO_TABLE_SIZE_FLOAT];

float lfo_sin(float phase_0_to_1);
