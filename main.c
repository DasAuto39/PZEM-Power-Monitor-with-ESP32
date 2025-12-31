#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_sleep.h"

// --- IMPORT MODULES ---
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "pzem_driver.h"
#include "ota_manager.h"

#define RELAY_PIN GPIO_NUM_23
static const char *TAG = "MAIN_APP";

#define DATA_THRESHOLD 1.0
#define CURRENT_THRESHOLD 0.1

// 90s to give OTA time to finish
#define IDLE_TIMEOUT_MS 90000 
#define SLEEP_DURATION 5
//sleep cycles before forcing a check (60 * 5s = 300s = 5 mins)
#define OTA_CHECK_CYCLES 60   

uint32_t loop_counter = 0;

// RTC MEMORY (Survives Deep Sleep) ---
RTC_DATA_ATTR static pzem_data_t last_saved_data;
RTC_DATA_ATTR static bool has_run_before = false;
// NEW: Counter to track sleep cycles
RTC_DATA_ATTR static int wake_count_for_ota = 0; 

// --- MAIN LOGIC TASK ---
void sensor_logic_task(void *arg) {
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RELAY_PIN, 1); 

    bool overload_active = false;
    uint32_t overload_timer = 0;
    
    uint32_t last_change_time = pdTICKS_TO_MS(xTaskGetTickCount());

    while(1) {
        pzem_data_t data = pzem_read_registers();
        loop_counter++;
        bool relay_state_logical = false; 

        if (data.valid) {
            // 1. Overload Safety Logic
            if (data.power > 80.0) {
                if (!overload_active) {
                    gpio_set_level(RELAY_PIN, 0); 
                    overload_active = true;
                    overload_timer = pdTICKS_TO_MS(xTaskGetTickCount());
                    ESP_LOGE(TAG, "Overload Detected! Cutoff Triggered.");
                }
                relay_state_logical = true;
            } else {
                if (overload_active) {
                    if (pdTICKS_TO_MS(xTaskGetTickCount()) - overload_timer > 10000) {
                        gpio_set_level(RELAY_PIN, 1); 
                        overload_active = false;
                        relay_state_logical = false;
                        ESP_LOGI(TAG, "Overload Cleared.");
                    } else {
                        relay_state_logical = true;
                    }
                } else {
                    gpio_set_level(RELAY_PIN, 1);
                    relay_state_logical = false;
                }
            }

            // 2. Change Detection
            float power_diff = fabsf(data.power - last_saved_data.power);
            float current_diff = data.current - last_saved_data.current;
            float voltage_diff = fabsf(data.voltage - last_saved_data.voltage);
            
            
            bool is_change = (power_diff > DATA_THRESHOLD || current_diff > CURRENT_THRESHOLD || voltage_diff > DATA_THRESHOLD || (!has_run_before));
            bool read_5time = (loop_counter % 5 == 0);

            if(is_change) {
                ESP_LOGI(TAG, "Change detected. Sending MQTT (P: %.1f W)", power_diff);
                mqtt_send_pzem_data(data, relay_state_logical);
                

                last_saved_data = data;
                has_run_before = true;
                last_change_time = pdTICKS_TO_MS(xTaskGetTickCount());
            }
            
        
            else {
                ESP_LOGD(TAG, "No change. (Idle for %lu ms)", pdTICKS_TO_MS(xTaskGetTickCount()) - last_change_time);
            }

            // 3. Idle Timeout (Sleep)
            if (pdTICKS_TO_MS(xTaskGetTickCount()) - last_change_time > IDLE_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Idle Timeout (%dms). Entering Deep Sleep...", IDLE_TIMEOUT_MS); 
                esp_deep_sleep(SLEEP_DURATION * 1000000LL);
            }

            ESP_LOGI(TAG, "V: %.4f V | I: %.4f A | P: %.4f W | F: %0.4f | E: %0.4f kJ | PF : %0.4f", data.voltage, data.current, data.power, data.frequency, data.energy, data.pf);

        } else {
            ESP_LOGW(TAG, "Sensor Read Failed");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    pzem_init();

    // 1. Read Sensor 
    pzem_data_t boot_check = pzem_read_registers();
    
    // 2. Check Data Changes
    float p_diff = fabsf(boot_check.power - last_saved_data.power);

    // 3. Increment OTA Counter
    wake_count_for_ota++;
    
    // Check if it's time to force a wake-up for OTA (e.g. every 5 mins)
    bool force_ota_check = (wake_count_for_ota >= OTA_CHECK_CYCLES);
    // 4. Wake Up Decision
    bool wake_up_fully = (p_diff > DATA_THRESHOLD) || (!has_run_before) || force_ota_check;

    if (wake_up_fully) {
        ESP_LOGI(TAG, "Waking Up Fully");
        
        if (force_ota_check) {
            ESP_LOGI(TAG, "(Reason: Periodic OTA Check - Cycle %d)", wake_count_for_ota);
            wake_count_for_ota = 0; // Reset counter
        }

        wifi_init_sta();
        mqtt_manager_init();

        xTaskCreatePinnedToCore(sensor_logic_task, "sensor_task", 4096, NULL, 5, NULL, 1);
        xTaskCreatePinnedToCore(mqtt_publisher_task, "pub_task", 4096, NULL, 5, NULL, 0);
        xTaskCreate(ota_task, "ota_task", 8192, NULL, 3, NULL);
    } 
    else {

        ESP_LOGI(TAG, "No change (Cycle %d/%d). Sleeping...", wake_count_for_ota, OTA_CHECK_CYCLES);
        /*gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << RELAY_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);*/
        gpio_reset_pin(RELAY_PIN);
        gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);

        // 2. Set the Level (from RTC memory)
        gpio_set_level(RELAY_PIN, 1);
        
        // 3. Enable Hold (Lock the pin)
        gpio_hold_en(RELAY_PIN);
        gpio_deep_sleep_hold_en();
        esp_deep_sleep(SLEEP_DURATION * 1000000LL);
        
    }


    /*First version
    // 1. Initialize NVS (Required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Hardware & Network Modules
    ESP_LOGI(TAG, "Initializing Modules...");
    pzem_init();          // Setup UART
    wifi_init_sta();      // Setup WiFi & Event Loop
    mqtt_manager_init();  // Setup MQTT Client & Queue

    // 3. Create Tasks
    ESP_LOGI(TAG, "Starting Tasks...");

    // Task A: Sensor Logic (High Priority, Core 1)
    // Reads sensor, decides relay state, pushes to queue
    xTaskCreatePinnedToCore(sensor_logic_task, "sensor_task", 4096, NULL, 5, NULL, 1);
    
    // Task B: MQTT Publisher (Medium Priority, Core 0)
    // Reads queue, talks to WiFi stack
    xTaskCreatePinnedToCore(mqtt_publisher_task, "pub_task", 4096, NULL, 5, NULL, 0);
    
    // Task C: OTA Update Check (Low Priority, Any Core)
    // Checks GitHub every 5 mins
    xTaskCreate(ota_task, "ota_task", 8192, NULL, 3, NULL);
    */
}