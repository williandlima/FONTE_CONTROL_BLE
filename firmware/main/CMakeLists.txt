idf_component_register(
    SRCS "main.c" "ble_nus.c"
         "../core/psu.c" "../core/bridge.c" "../core/json.c"
    INCLUDE_DIRS "." "../core"
    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_driver_usb_serial_jtag
                  esp_timer vfs
)
