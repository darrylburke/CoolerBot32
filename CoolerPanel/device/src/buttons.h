#pragma once
// Physical side button (Key3 on the TCA9554 expander, EXIO4): each press
// cycles the backlight level (backlight_cycle). The same button also pulses
// the AXP2101 power key — deliberately NOT handled as well, or every press
// would double-step. Poll from the main loop.
void buttons_init();
void buttons_poll();
