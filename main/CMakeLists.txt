idf_component_register(
    SRCS
        "main.c"
        "ir_rmt.c"
        "sejin.c"
        "hid_keymap.c"
        "ble_kbd.c"
        "esp_hid_gap.c"
        "status_led.c"
        "boot_btn.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_rmt esp_driver_gpio esp_timer esp_hid bt nvs_flash esp_event esp_hw_support
)
