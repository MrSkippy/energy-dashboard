// Rotary encoder knob (CrowPanel 1.46"), decoded in hardware by the PCNT unit.

#ifndef KNOB_H
#define KNOB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Non-fatal: on failure logs a warning and knob_take_steps() keeps returning 0.
void knob_init(void);

// Detents turned since the previous call; positive = clockwise.
int32_t knob_take_steps(void);

// Push button (pressing the knob) is held down right now.
bool knob_button_pressed(void);

#ifdef __cplusplus
}
#endif

#endif
