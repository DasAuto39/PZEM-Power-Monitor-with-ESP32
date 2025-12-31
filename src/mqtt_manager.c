#include "mqtt_manager.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

#define MQTT_BROKER_URI "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC      "esp32/pzem/data"

static const char *TAG = "MQTT_MGR";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static QueueHandle_t mqtt_queue = NULL;

typedef struct {
    char payload[256];
} mqtt_msg_t;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Connected to Broker");
        mqtt_connected = true;
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from Broker");
        mqtt_connected = false;
    }
}

void mqtt_manager_init(void) {
    mqtt_queue = xQueueCreate(10, sizeof(mqtt_msg_t));
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_send_pzem_data(pzem_data_t data, bool relay_state) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "voltage", data.voltage);
    cJSON_AddNumberToObject(root, "current", data.current);
    cJSON_AddNumberToObject(root, "power", data.power);
    cJSON_AddNumberToObject(root, "energy", data.energy);
    cJSON_AddNumberToObject(root, "frequency", data.frequency);
    cJSON_AddNumberToObject(root, "pf", data.pf);
    cJSON_AddBoolToObject(root, "relay", relay_state);
    
    char *json_str = cJSON_PrintUnformatted(root);
    
    mqtt_msg_t msg;
    strncpy(msg.payload, json_str, sizeof(msg.payload)-1);
    msg.payload[sizeof(msg.payload)-1] = '\0'; // Ensure null term
    
    if (xQueueSend(mqtt_queue, &msg, 0) != pdPASS) {
        ESP_LOGW(TAG, "Queue full, dropping packet");   
    }

    free(json_str);
    cJSON_Delete(root);
}

void mqtt_publisher_task(void *arg) {
    mqtt_msg_t incoming;
    while(1) {
        if (xQueueReceive(mqtt_queue, &incoming, portMAX_DELAY)) {
            if (mqtt_connected) {
                esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, incoming.payload, 0, 1, 0);
            }
        }
    }
}