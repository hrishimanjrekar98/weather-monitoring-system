#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h" 
#include "driver/i2c_master.h" 
#include "dht.h"               
//#include "ssd1306.h"
#include "nvs_flash.h"         // Added for Wi-Fi
#include "esp_wifi.h"          // Added for Wi-Fi
#include "esp_event.h"         // Added for Wi-Fi
#include "mqtt_client.h"       // Added for MQTT
#include "host/ble_hs.h" 
#include "freertos/timers.h"
#include "esp_crt_bundle.h"

#include "led_strip.h"
#include "esp_log.h"
#include "esp_err.h"

#define BLINK_GPIO 48

static led_strip_handle_t led_strip;

// --- Network Credentials ---
#define WIFI_SSID "S23"
#define WIFI_PASS "Hrishi53@h"
#define MQTT_BROKER "mqtts://bfc1544d121142dd8c36774c1e7a7e14.s1.eu.hivemq.cloud:8883"
#define MQTT_TOPIC  "weather/data"

static const char *TAG = "IOT_NODE";

TimerHandle_t upload_timer;
uint32_t current_upload_interval_ms = 10000; // Default: 10 seconds

// --- Data Structures & Handles ---
typedef struct {
    float temperature;
    float humidity;
    int aqi_ppm;
} sensor_data_t;

// 1. Define the System States
typedef enum {
    STATE_BOOTING,
    STATE_WIFI_CONNECTING,
    STATE_MQTT_CONNECTING,
    STATE_NOMINAL,
    STATE_SENSOR_ERROR,
    STATE_GAS_WARNING,
    STATE_GAS_CRITICAL
} SystemState_t;

// QueueHandle_t display_queue;
QueueHandle_t mqtt_queue;
QueueHandle_t ble_queue;
QueueHandle_t timer_queue;

#define DHT_PIN         GPIO_NUM_5      
#define DHT_TYPE        DHT_TYPE_AM2301 
#define MQ135_ADC_CHAN  ADC_CHANNEL_3   
#define I2C_SDA         GPIO_NUM_8
#define I2C_SCL         GPIO_NUM_9

// SSD1306_t oled_dev;
esp_mqtt_client_handle_t mqtt_client = NULL;

// Global state variable (volatile so tasks can update it safely)
volatile SystemState_t current_sys_state = STATE_BOOTING;

// --- 1. The Timer Callback ---
// This runs instantly when the timer fires. NEVER block or use vTaskDelay here!
void timer_trigger_callback(TimerHandle_t xTimer) {
    uint8_t trigger_signal = 1; // Simple dummy variable to send
    
    // Send the signal to the back of the queue without waiting (0 delay)
    xQueueSend(timer_queue, &trigger_signal, 0); 
}

// 1. Hardware Initialization Function
void configure_led(void) {
    ESP_LOGI("LED", "Initializing addressable RGB LED");
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // You only have 1 on-board LED
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz resolution
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Ensure the LED is completely off at boot
    led_strip_clear(led_strip);
}

// 2. The Control Function (with brightness scaling)
void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    if (led_strip) {
        // Scale brightness to ~10% (divide by 10)
        // On-board LEDs are often very bright; this makes them comfortable to look at.
        uint8_t r_dim = r / 10;
        uint8_t g_dim = g / 10;
        uint8_t b_dim = b / 10;

        // Set the color for LED index 0
        led_strip_set_pixel(led_strip, 0, r_dim, g_dim, b_dim);
        
        // Push the new color data to the hardware
        led_strip_refresh(led_strip);
    }
}

// --- Wi-Fi & MQTT Event Handlers ---
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        current_sys_state = STATE_WIFI_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        current_sys_state = STATE_WIFI_CONNECTING;
        esp_wifi_connect();
        ESP_LOGW(TAG, "Disconnected from Wi-Fi. Retrying...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        current_sys_state = STATE_MQTT_CONNECTING;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        current_sys_state = STATE_NOMINAL;
        ESP_LOGI(TAG, "Connected to MQTT Broker!");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        current_sys_state = STATE_MQTT_CONNECTING;
        ESP_LOGW(TAG, "Disconnected from MQTT Broker.");
    }
}

// --- Network Setup Functions ---
void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = MQTT_BROKER,
            .verification.crt_bundle_attach = esp_crt_bundle_attach,
        },
        .credentials = {
            .username = "esp32_mqtt",
            .authentication = {
                .password = "Testmqtt123", // Must be nested inside authentication!
            },
            .client_id = "bfc1544d121142dd8c36774c1e7a7e14",
        }
    };

    // 2. FORCE THE ESP32 TO CONFIRM THE STRINGS IN MEMORY
    printf("\n--- PRE-FLIGHT SECURITY CHECK ---\n");
    printf("Target URI: %s\n", mqtt_cfg.broker.address.uri);
    printf("Username: '%s'\n", mqtt_cfg.credentials.username);
    printf("Password: '%s'\n", mqtt_cfg.credentials.authentication.password);
    printf("-----------------------------------\n\n");

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// --- 1. Sensor Task ---
void vSensorTask(void *pvParameters) {
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config, &adc1_handle);
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12 
    };
    adc_oneshot_config_channel(adc1_handle, MQ135_ADC_CHAN, &config);

    uint8_t received_signal;
    sensor_data_t current_data;

    while (1) {
        
        if(xQueueReceive(timer_queue, &received_signal, portMAX_DELAY) == pdTRUE){
            float temp = 0.0, hum = 0.0;
            if (dht_read_float_data(DHT_TYPE, DHT_PIN, &hum, &temp) == ESP_OK) {
                current_data.temperature = temp;
                current_data.humidity = hum;
                if (current_sys_state == STATE_SENSOR_ERROR) {
                    current_sys_state = STATE_NOMINAL;
                }
            } else {
                ESP_LOGE(TAG, "Could not read data from DHT sensor");
                current_sys_state = STATE_SENSOR_ERROR;
            }

            int adc_raw;
            adc_oneshot_read(adc1_handle, MQ135_ADC_CHAN, &adc_raw);
            current_data.aqi_ppm = adc_raw; 

            // Update state based on AQI only if we are in NOMINAL or GAS states
            if (current_sys_state == STATE_NOMINAL || 
                current_sys_state == STATE_GAS_WARNING || 
                current_sys_state == STATE_GAS_CRITICAL) {
                if (adc_raw > 2000) {
                    current_sys_state = STATE_GAS_CRITICAL;
                } else if (adc_raw > 1000) {
                    current_sys_state = STATE_GAS_WARNING;
                } else {
                    current_sys_state = STATE_NOMINAL;
                }
            }

            //xQueueSend(display_queue, &current_data, 0);
            xQueueSend(mqtt_queue, &current_data, 0);
            xQueueSend(ble_queue, &current_data, 0);
        }

        //vTaskDelay(pdMS_TO_TICKS(5000)); // Slowed down to 5 seconds for polite MQTT publishing
    }
}

// --- 2. Display Task ---
// void vDisplayTask(void *pvParameters) {
//     sensor_data_t data;
//     char buffer[32];

//     while (1) {
//         if (xQueueReceive(display_queue, &data, portMAX_DELAY) == pdTRUE) {
//             ssd1306_clear_screen(&oled_dev, false);
//             snprintf(buffer, sizeof(buffer), "Temp: %.1f C", data.temperature);
//             ssd1306_display_text(&oled_dev, 0, buffer, strlen(buffer), false);
//             snprintf(buffer, sizeof(buffer), "Hum:  %.1f %%", data.humidity);
//             ssd1306_display_text(&oled_dev, 2, buffer, strlen(buffer), false);
//             snprintf(buffer, sizeof(buffer), "AQI:  %d", data.aqi_ppm);
//             ssd1306_display_text(&oled_dev, 4, buffer, strlen(buffer), false);
//         }
//     }
// }

// 2. The Dedicated LED Task
// 3. The Dedicated LED Task
void led_status_task(void *pvParameter) {
    while(1) {
        switch(current_sys_state) {
            case STATE_BOOTING:
                set_led_color(255, 255, 255); // Solid White
                vTaskDelay(pdMS_TO_TICKS(100)); 
                break;

            case STATE_WIFI_CONNECTING:
                set_led_color(0, 0, 255); // Blue
                vTaskDelay(pdMS_TO_TICKS(500));
                set_led_color(0, 0, 0);   // Off
                vTaskDelay(pdMS_TO_TICKS(500));
                break;

            case STATE_MQTT_CONNECTING:
                set_led_color(0, 0, 255); // Fast Blue
                vTaskDelay(pdMS_TO_TICKS(100));
                set_led_color(0, 0, 0);   // Off
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case STATE_NOMINAL:
                set_led_color(0, 255, 0); // Green
                vTaskDelay(pdMS_TO_TICKS(50));
                set_led_color(0, 0, 0);   // Off
                vTaskDelay(pdMS_TO_TICKS(4950)); // Sleep for 5 seconds
                break;

            case STATE_SENSOR_ERROR:
                set_led_color(255, 0, 0); // Fast Red
                vTaskDelay(pdMS_TO_TICKS(100));
                set_led_color(0, 0, 0);   // Off
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            // ---> THE MISSING GAS CASES <---
            case STATE_GAS_WARNING:
                set_led_color(255, 255, 0); // Solid Yellow
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case STATE_GAS_CRITICAL:
                set_led_color(255, 0, 0); // Red
                vTaskDelay(pdMS_TO_TICKS(100));
                set_led_color(255, 255, 0); // Yellow
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            // ---> STRICT COMPILER CATCH-ALL <---
            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

// --- 3. Cloud / MQTT Task ---
void vMqttTask(void *pvParameters) {
    sensor_data_t data;

    while (1) {
        if (xQueueReceive(mqtt_queue, &data, portMAX_DELAY) == pdTRUE) {
            if (mqtt_client != NULL) {
                // Format data as a JSON string
                char json_payload[100];
                sprintf(json_payload, "{\"temperature\":%.1f,\"humidity\":%.1f,\"aqi\":%d}", data.temperature, data.humidity, data.aqi_ppm);
                
                // 3. Force the exact length
                int payload_len = strlen(json_payload);

                // 4. Publish with the explicit length (replace "weather/data" with your actual topic)
                int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_payload, payload_len, 1, 0);
                ESP_LOGI(TAG, "MQTT  ret: %d", msg_id);
                // 5. Add the debug check so we know if the ESP32 actually queued it
                if (msg_id != -1) {
                    printf("SUCCESS! Length: %d. ID: %d. Payload: %s\n", payload_len, msg_id, json_payload);
                } else {
                    printf("ERROR: Publish function returned -1\n");
                }
            }
        }
    }
}

// --- 4. BLE Task (Silent for now) ---
void vBleTask(void *pvParameters) {
    sensor_data_t data;
    while (1) {
        if (xQueueReceive(ble_queue, &data, portMAX_DELAY) == pdTRUE) {
            // Pending BLE GATT implementation
        }
    }
}

// --- Main App ---
void app_main(void) {
    ESP_LOGI(TAG, "System Booting...");

    // 0. Initialize LED hardware
    configure_led();

    // 1. Initialize NVS (Required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Queues
    //display_queue = xQueueCreate(5, sizeof(sensor_data_t));
    mqtt_queue = xQueueCreate(5, sizeof(sensor_data_t));
    ble_queue = xQueueCreate(5, sizeof(sensor_data_t));
    timer_queue = xQueueCreate(5, sizeof(uint8_t));

    // 3. Initialize Hardware & Networking
    // i2c_master_init(&oled_dev, I2C_SDA, I2C_SCL, -1);
    // ssd1306_init(&oled_dev, 128, 64);
    // ssd1306_clear_screen(&oled_dev, false);
    
   // char *boot_msg = "Connecting Wi-Fi...";
   // ssd1306_display_text(&oled_dev, 3, boot_msg, strlen(boot_msg), false);

    wifi_init_sta();
    mqtt_app_start();

    // 4. Spawn Tasks
    xTaskCreatePinnedToCore(vSensorTask, "SensorTask", 4096, NULL, 5, NULL, 1);
    //xTaskCreatePinnedToCore(vDisplayTask, "DisplayTask", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(vMqttTask, "MqttTask", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(vBleTask, "BleTask", 4096, NULL, 3, NULL, 0);

    // Start the LED task immediately with a low priority (e.g., 1)
    xTaskCreate(led_status_task, "led_task", 2048, NULL, 1, NULL);

    // Create the Software Timer (Auto-reloading)
    upload_timer = xTimerCreate(
        "UploadTimer",                      // Text name
        pdMS_TO_TICKS(current_upload_interval_ms), // Configurable interval
        pdTRUE,                             // pdTRUE = Auto-reload (repeating)
        (void *)0,                          // Timer ID
        timer_trigger_callback              // Callback function
    );

    // Start the timer
    xTimerStart(upload_timer, 0);
}