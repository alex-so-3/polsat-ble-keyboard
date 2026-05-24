#include "status_led.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"

// State-machine tick. 50 Hz is fast enough for a smooth blink and short flashes
// while costing essentially nothing.
#define LED_TICK_US        20000

#define BOOT_ON_MS         1000    // solid on for 1 s after power-up
#define PAIR_BLINK_MS      150     // on/off half-period -> ~3.3 Hz pairing blink
#define ACTIVITY_FLASH_MS  40      // very short flash on a valid IR frame

static esp_timer_handle_t s_timer;
static int  s_gpio;
static bool s_active_low;
static int  s_last_level = -1;     // -1 = nothing written yet

static volatile bool    s_pairing;
static volatile int64_t s_boot_until_us;
static volatile int64_t s_activity_until_us;

static void led_write(bool on) {
    int level = on ^ s_active_low ? 1 : 0;  // active_low inverts the drive level
    if (level != s_last_level) {
        gpio_set_level(s_gpio, level);
        s_last_level = level;
    }
}

static void led_tick(void *arg) {
    int64_t now = esp_timer_get_time();
    bool on;
    if (now < s_boot_until_us) {
        on = true;                                      // boot indication
    } else if (s_pairing) {
        on = ((now / (PAIR_BLINK_MS * 1000)) & 1) == 0; // pairing blink
    } else if (now < s_activity_until_us) {
        on = true;                                      // IR-activity flash
    } else {
        on = false;                                     // idle
    }
    led_write(on);
}

void status_led_init(int gpio_num, bool active_low) {
    s_gpio = gpio_num;
    s_active_low = active_low;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // Start the boot indication immediately, then let the timer maintain it.
    s_boot_until_us = esp_timer_get_time() + (int64_t)BOOT_ON_MS * 1000;
    led_write(true);

    const esp_timer_create_args_t a = { .callback = led_tick, .name = "status_led" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, LED_TICK_US));
}

void status_led_set_pairing(bool on) {
    s_pairing = on;
}

void status_led_activity(void) {
    s_activity_until_us = esp_timer_get_time() + (int64_t)ACTIVITY_FLASH_MS * 1000;
}
