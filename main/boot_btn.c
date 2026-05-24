#include "boot_btn.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"

#define BTN_ACTIVE_LOW  1            // BOOT shorts the pin to GND when pressed
#define POLL_US         30000        // 30 ms poll -> built-in debounce
#define HOLD_MS         1500
#define HOLD_TICKS      (HOLD_MS / (POLL_US / 1000))

static esp_timer_handle_t s_timer;
static int   s_gpio;
static void (*s_cb)(void);
static int   s_held;                 // consecutive pressed samples
static bool  s_fired;                // latched so one hold fires once

static bool pressed(void) {
    int lvl = gpio_get_level(s_gpio);
    return BTN_ACTIVE_LOW ? (lvl == 0) : (lvl != 0);
}

static void btn_tick(void *arg) {
    if (pressed()) {
        if (!s_fired) {
            if (++s_held >= HOLD_TICKS) {
                s_fired = true;
                if (s_cb) s_cb();
            }
        }
    } else {
        s_held = 0;
        s_fired = false;
    }
}

void boot_btn_init(int gpio_num, void (*on_long_press)(void)) {
    s_gpio = gpio_num;
    s_cb = on_long_press;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    const esp_timer_create_args_t a = { .callback = btn_tick, .name = "boot_btn" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, POLL_US));
}
