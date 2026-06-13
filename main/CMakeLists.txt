idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_https_ota esp_http_client esp_adc esp_driver_i2c ssd1306 esp_driver_spi led_strip)