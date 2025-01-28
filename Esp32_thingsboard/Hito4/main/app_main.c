#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "portmacro.h"
#include "protocol_examples_common.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "driver/gpio.h"

#include "cJSON.h"

static const char *TAG = "mqtt_example";
static esp_mqtt_client_handle_t client;
static bool connected = false;
static int msg_id;
static bool thingsboard = false;

#define readDoorPin 12
#define readLockPin 14
#define writeDoorPin 27
#define writeLockPin 26
#define DEFAULT_VREF    1100  


static esp_adc_cal_characteristics_t *adc_chars;
#if CONFIG_IDF_TARGET_ESP32
static const adc_channel_t channel = ADC_CHANNEL_0;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#elif CONFIG_IDF_TARGET_ESP32S2
static const adc_channel_t channel = ADC_CHANNEL_6;     // GPIO7 if ADC1, GPIO17 if ADC2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;

static void check_efuse(void)
{
#if CONFIG_IDF_TARGET_ESP32
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
#elif CONFIG_IDF_TARGET_ESP32S2
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
    }
#else
#error "This example is configured for ESP32/ESP32S2."
#endif
}

static void configure_gpio(void)
{
    gpio_reset_pin(readDoorPin);
    gpio_reset_pin(readLockPin);
    gpio_reset_pin(writeDoorPin);
    gpio_reset_pin(writeLockPin);

    gpio_set_direction(readDoorPin, GPIO_MODE_INPUT);
    gpio_set_direction(readLockPin, GPIO_MODE_INPUT);
    gpio_set_direction(writeDoorPin, GPIO_MODE_OUTPUT);
    gpio_set_direction(writeLockPin, GPIO_MODE_OUTPUT);

}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        esp_mqtt_client_subscribe(event->client, "v1/devices/me/rpc/request/+", 0);
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        // Procesar RPC
            if (strncmp(event->topic, "v1/devices/me/rpc/request/", 26) == 0) {
                int request_id = atoi(event->topic + 26); // Extraer ID de la solicitud
                ESP_LOGI(TAG, "RPC Request ID: %d", request_id);

                // Aquí procesas el comando (ejemplo: encender LED)
                if (strstr(event->data, "\"method\":\"abrir\"")) {
                    ESP_LOGI(TAG, "Mandando señal.....");
                    gpio_set_level(writeDoorPin, 1);
                    thingsboard = true;
                }
                if (strstr(event->data, "\"method\":\"cerrar\"")) {
                    ESP_LOGI(TAG, "Mandando señal.....");
                    gpio_set_level(writeDoorPin, 0);
                    thingsboard = true;
                }
                if (strstr(event->data, "\"method\":\"desbloquear\"")) {
                    ESP_LOGI(TAG, "Mandando señal.....");
                    gpio_set_level(writeLockPin, 0);
                    thingsboard = true;
                }
                if (strstr(event->data, "\"method\":\"bloquear\"")) {
                    ESP_LOGI(TAG, "Mandando señal.....");
                    gpio_set_level(writeLockPin, 1);
                    thingsboard = true;
                }
            }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = "mqtt://mXp38j5nRgARhj8N1r6R@demo.thingsboard.io:1883",
    };
    
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

void app_main(void)
{
    configure_gpio();
    //Check if Two Point or Vref are burned into eFuse
    check_efuse();

    if (unit == ADC_UNIT_1) {
        adc1_config_width(width);
        adc1_config_channel_atten(channel, atten);
    } else {
        adc2_config_channel_atten((adc2_channel_t)channel, atten);
    }

    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
    int ant_door = -1;
    int ant_lock = -1;
    int door = gpio_get_level(readDoorPin);
    int lock = gpio_get_level(readLockPin);
    while(true)
    {
		if(connected)
		{
            
            door = gpio_get_level(readDoorPin);
            lock = gpio_get_level(readLockPin);
            ESP_LOGI(TAG, "Valor devuelto %d, %d", door, lock);
            if(!thingsboard)
            {
                if(door != ant_door)
                {
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddNumberToObject(root, "AbiertoCerrado", door); // En la telemetría de Thingsboard aparecerá
                    char *post_data = cJSON_PrintUnformatted(root);
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0);
                }
                if(lock != ant_lock)
                {
                    cJSON *root = cJSON_CreateObject();
                    cJSON_AddNumberToObject(root, "EstadoBloqueo", lock); // En la telemetría de Thingsboard aparecerá
                    char *post_data = cJSON_PrintUnformatted(root);
                    msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0);
                }
                ant_door = door;
                ant_lock = lock;
            }
            else
            {
                thingsboard = false;
            }
            /*adc_reading = adc1_get_raw((adc1_channel_t)channel);
            printf("Raw: %ld\t", adc_reading);
			cJSON *root = cJSON_CreateObject();
			cJSON_AddNumberToObject(root, "AbiertoCerrado", adc_reading); // En la telemetría de Thingsboard aparecerá
			char *post_data = cJSON_PrintUnformatted(root);
	        msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", post_data, 0, 1, 0);*/
		}
		vTaskDelay(300 / portTICK_PERIOD_MS);
		
	}
}
