#include "ota_manager.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Malas pakai file sendiri
#define GITHUB_JSON_URL "xxxxxxx"

// Ensure this token has "Repo" scope permissions in GitHub Developer Settings
#define GITHUB_PAT      "xxxxxx"

#define GITHUB_TOKEN_RAW "xxxxxx"

#define CURRENT_VERSION 3.0

static const char *TAG = "OTA_MGR";

static void cleanup_url(char *url) {
    if (!url) return;
    char *src = url, *dst = url;
    while (*src) {
        if (!isspace((unsigned char)*src)) {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}


// FIXED: Robust Event Handler to prevent Garbage/Overflow
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    static int output_len;
    
    // Reset length when a new request starts
    if (evt->event_id == HTTP_EVENT_ERROR || evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        output_len = 0;
    }
    
    if(evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (evt->user_data) {
                // Safety 1: Prevent buffer overflow (leave 1 byte for null terminator)
                if (output_len + evt->data_len < 2047) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                    // Safety 2: FORCE Null Termination immediately
                    ((char*)evt->user_data)[output_len] = '\0'; 
                }
            }
        }
    } 
    else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        output_len = 0;
    }
    return ESP_OK;
}

static void run_ota_update(const char* url) {
    cleanup_url(url);
    ESP_LOGI(TAG, "Starting OTA from: %s", url);
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
      
        .username = "token",       
        .password = GITHUB_TOKEN_RAW,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    esp_https_ota_config_t ota_config = { .http_config = &config };
    
    if (esp_https_ota(&ota_config) == ESP_OK) {
        ESP_LOGI(TAG, "OTA Success. Restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA Failed");
    }
}

void ota_task(void *pvParam) {
  
    char *local_response_buffer = calloc(1, 2048);
    
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1 * 10 * 1000)); // Check every 60s for testing
        ESP_LOGI(TAG, "Checking for updates...");
        
        esp_http_client_config_t config = {
            .url = GITHUB_JSON_URL,
            .method = HTTP_METHOD_GET,
            .event_handler = _http_event_handler,
            .user_data = local_response_buffer,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_redirect = true,
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        // This sends the token correctly
        esp_http_client_set_header(client, "Authorization", GITHUB_PAT);
        esp_http_client_set_header(client, "Cache-Control", "no-cache");
        esp_http_client_set_header(client, "Pragma", "no-cache");
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            // Get the actual HTTP Code (e.g., 200, 404, 401)
            int status_code = esp_http_client_get_status_code(client);
            
            if (status_code == 200) {
                ESP_LOGI(TAG, "JSON Received: %s", local_response_buffer);
                cJSON *json = cJSON_Parse(local_response_buffer);
                if (json) {
                    cJSON *ver = cJSON_GetObjectItem(json, "version");
                    cJSON *url = cJSON_GetObjectItem(json, "update_file_url");
                    if (cJSON_IsString(ver) && cJSON_IsString(url)) {
                        if (atof(ver->valuestring) > CURRENT_VERSION) {
                            ESP_LOGW(TAG, "New version: %s (Current: %.2f)", ver->valuestring, CURRENT_VERSION);
                            run_ota_update(url->valuestring);
                        } else {
                            ESP_LOGI(TAG, "Up to date. Cloud version: %s", ver->valuestring);
                        }
                    }
                    cJSON_Delete(json);
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON. Content: %s", local_response_buffer);
                }
            } 
            else if (status_code == 404) {
                ESP_LOGE(TAG, "404 Error: File not found."); 
                ESP_LOGE(TAG, "Likely Cause: Token invalid, expired, or wrong Repo URL.");
                ESP_LOGE(TAG, "Server Message: %s", local_response_buffer);
            }
            else {
                ESP_LOGE(TAG, "HTTP Error %d. Response: %s", status_code, local_response_buffer);
            }
        } else {
            ESP_LOGE(TAG, "Connection Failed: %s", esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        // Clear buffer for next loop
        memset(local_response_buffer, 0, 2048);
    }
    free(local_response_buffer);
}