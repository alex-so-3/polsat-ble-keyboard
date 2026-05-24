#pragma once

// Poll the BOOT button (active-low, with the chip's internal pull-up) and call
// `on_long_press` once each time it is held continuously for ~1.5 s. Releasing
// the button re-arms it for the next hold.
//
// The callback runs in the esp_timer task, so keep it short: set a flag and do
// the heavy work elsewhere rather than blocking inside it.
void boot_btn_init(int gpio_num, void (*on_long_press)(void));
