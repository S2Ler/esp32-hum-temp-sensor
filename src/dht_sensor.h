#pragma once

#include <freertos/task.h>

typedef void (*DHT_sensor_handle_t)(float humidity, float temperature);

struct DHT_sensor {
    TaskHandle_t task_handle;    
};

typedef struct DHT_sensor * DHT_sensor_t;

DHT_sensor_t DHT_sensor_create(int gpio, DHT_sensor_handle_t handle, int read_delay_ms);
void DHT_sensor_delete(DHT_sensor_t);
