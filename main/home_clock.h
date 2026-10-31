// Drives the Home screen clock labels from the system time.

#ifndef HOME_CLOCK_H
#define HOME_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once after GUI_init(): the label pointers are set there.
void home_clock_start(void);

#ifdef __cplusplus
}
#endif

#endif
