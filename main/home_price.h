// Shows the current electricity price and trend on the Home screen.

#ifndef HOME_PRICE_H
#define HOME_PRICE_H

#ifdef __cplusplus
extern "C" {
#endif

// Call once after GUI_init(). Shows the prices from tibber.c.
void home_price_start(void);

#ifdef __cplusplus
}
#endif

#endif
