#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "dht_sensor.h"
#include "mqtt_client.h"
#include "secrets.h"
#include <TemperatureWithHumidity.h>

#define CONFIG_ESP_MAXIMUM_RETRY 10

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group = NULL;
static EventGroupHandle_t s_mqtt_client_event_group = NULL;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static DHT_sensor_t dht_sensor = NULL;

const int WIFI_CONNECTED_BIT = BIT0;
const int MQTT_CLIENT_CONNECTED_BIT = BIT0;

static const char *TAG = "wifi station";

static int s_retry_num = 0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                s_retry_num++;
                ESP_LOGI(TAG,"retry to connect to the AP");
            }
            ESP_LOGI(TAG,"connect to the AP fail\n");
            break;
        }
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "wifi connected");
}

static void sensor_handle(float humidity, float temperature) {
    configASSERT(mqtt_client != NULL);
    if (!(xEventGroupGetBits(s_mqtt_client_event_group) & MQTT_CLIENT_CONNECTED_BIT)) {
         ESP_LOGI(TAG, "Sensor date received, but mqtt client isn't connected");
         return;
    };

    ESP_LOGI(TAG, "temperature: %f; humidity: %f", temperature, humidity);

    TemperatureWithHumidity values;
    values.temperatureInCelsius = temperature;
    values.humidityInPercent = humidity;
    char *payload = TemperatureWithHumidityEncode(&values);
    if (payload) {
        ESP_LOGI(TAG, "payload: %s", payload);
        esp_mqtt_client_publish(mqtt_client, "temperature_humidity", payload, 0, 0, 0);
        free(payload);
    }
    else {
        ESP_LOGE(TAG, "Incorrect payload: %lf, %lf", temperature, humidity);
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(s_mqtt_client_event_group, MQTT_CLIENT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(s_mqtt_client_event_group, MQTT_CLIENT_CONNECTED_BIT);
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void start_mqtt_client() {
    s_mqtt_client_event_group = xEventGroupCreate();

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_MQTT_CLIENT_URI,
        .client_id = CONFIG_MQTT_CLIENT_ID,
        .username = CONFIG_MQTT_USERNAME,
        .password = CONFIG_MQTT_PASSWORD,
        .event_handle = mqtt_event_handler,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    configASSERT(mqtt_client != NULL);
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
    xEventGroupWaitBits(s_mqtt_client_event_group, MQTT_CLIENT_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "start_mqtt_client finished");
}

static void start_dht_sensor() {
    dht_sensor = DHT_sensor_create(4, sensor_handle, 120000);
}

void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    ESP_LOGI(TAG, "Ready to init mqtt client");
    start_mqtt_client();
    start_dht_sensor();
}