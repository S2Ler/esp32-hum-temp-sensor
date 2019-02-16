#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_system.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <dht.h>
#include "dht_sensor.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))

static const char *TAG = "DHT";

typedef struct {
    int gpio;
    int read_delay_ms;
    DHT_sensor_handle_t handle;
} DHT_task_parameters;


void DHT_task(void *pvParameter)
{
    DHT_task_parameters *parameters = pvParameter;
    ESP_LOGI(TAG, "Starting DHT Task\n\n");
    
    int16_t temperature = 0;
    int16_t humidity = 0;

    while (1)
    {
        ESP_LOGI(TAG, "=== Reading DHT ===\n");
        if (dht_read_data(DHT_TYPE_DHT22, parameters->gpio, &humidity, &temperature) == ESP_OK) {
            ESP_LOGI(TAG, "Humidity: %f%% Temp: %fC\n", (float)humidity / 10.0, (float)temperature / 10.0);
        } else {
            printf("Could not read data from sensor\n");
        }
        parameters->handle((float)humidity / 10.0, (float)temperature / 10.0);

        // -- wait at least 2 sec before reading again ------------
        // The interval of whole process must be beyond 2 seconds !!
        vTaskDelay(parameters->read_delay_ms / portTICK_RATE_MS);
    }
}

DHT_sensor_t DHT_sensor_create(int gpio, DHT_sensor_handle_t handle, int read_delay_ms) {
    DHT_task_parameters *parameters = malloc(sizeof(DHT_task_parameters));
    parameters->gpio = gpio;
    parameters->read_delay_ms = MAX(read_delay_ms, 2000);
    parameters->handle = handle;

    TaskHandle_t task_handle = NULL;
    xTaskCreate(&DHT_task, "DHT_sensor", 2048, parameters, 5, &task_handle);
    configASSERT(task_handle != NULL);

    DHT_sensor_t sensor = malloc(sizeof(struct DHT_sensor));
    sensor->task_handle = task_handle;
    return sensor;
}

void DHT_sensor_delete(DHT_sensor_t sensor)
{    
    vTaskDelete(sensor->task_handle);
    free(sensor);
}