#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_http_server.h"
#include "esp_system.h"

//Variables bluetooth
uint8_t ble_addr_type;
void ble_app_advertise(void);
int bt_check = 0;
//Variables WiFi
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN
#define pin1 12
#define motor1 27
#define motor2 26
#define motor3 25
#define motor4 33
static const char *TAG = "Control";

//variables acceso puerta y cerrojo
int acceso = 0;
int puerta_abierta = 0;
int bloqueada = 1;
static uint8_t open_door = 0;
static bool servidor = false;

//pines para interconectar las placas
#define readDoorPin 13
#define readLockPin 34
#define writeDoorPin 32
#define writeLockPin 14

//BLUETOOTH START
// Escribe datos al servidor bluetooth
static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // printf("Data from the client: %.*s\n", ctxt->om->om_len, ctxt->om->om_data);

    char * data = (char *)ctxt->om->om_data;
    printf("Data from the client: %.*s\n", ctxt->om->om_len, ctxt->om->om_data);
    return 0;
}

// Lee datos del servidor bluetooth
static int device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    os_mbuf_append(ctxt->om, "Data from the server", strlen("Data from the server"));
    return 0;
}

// Estructuras de acciones bluetooth
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180),                 // Define UUID for device type
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0xFEF4),           // Define UUID for reading
          .flags = BLE_GATT_CHR_F_READ,
          .access_cb = device_read},
         {.uuid = BLE_UUID16_DECLARE(0xDEAD),           // Define UUID for writing
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = device_write},
         {0}}},
    {0}};

// manejador BLE
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    // Advertise if connected
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        bt_check ++;
        if (event->connect.status != 0)
        {
            ble_app_advertise();
        }
        break;
    // Advertise again after completion of the event
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECTED");
        bt_check --;
        ble_app_advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI("GAP", "BLE GAP EVENT");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}

// define la conexion bluetooth
void ble_app_advertise(void)
{
    // GAP - device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name(); // Read the BLE device name
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // GAP - device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; 
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; 
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

// la aplicacion bluetooth
void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0, &ble_addr_type); 
    ble_app_advertise();                    
}

// tarea en bucle
void host_task(void *param)
{
    nimble_port_run();
}  

void set_pins(void)
{
    gpio_reset_pin(pin1);

    gpio_reset_pin(motor1);
    gpio_reset_pin(motor2);
    gpio_reset_pin(motor3);
    gpio_reset_pin(motor4);

    gpio_reset_pin(readDoorPin);
    gpio_reset_pin(writeDoorPin);
    gpio_reset_pin(readLockPin);
    gpio_reset_pin(writeLockPin);

    gpio_set_direction(pin1, GPIO_MODE_OUTPUT);

    gpio_set_direction(motor1, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor2, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor3, GPIO_MODE_OUTPUT);
    gpio_set_direction(motor4, GPIO_MODE_OUTPUT);

    gpio_set_direction(readDoorPin, GPIO_MODE_INPUT);
    gpio_set_direction(writeDoorPin, GPIO_MODE_OUTPUT);
    gpio_set_direction(readLockPin, GPIO_MODE_INPUT);
    gpio_set_direction(writeLockPin, GPIO_MODE_OUTPUT);

    gpio_set_level(pin1, open_door);
}

void step1(void)
{
    gpio_set_level(motor1, 1);
    gpio_set_level(motor2, 0);
    gpio_set_level(motor3, 1);
    gpio_set_level(motor4, 0);
}

void step2(void)
{
    gpio_set_level(motor1, 1);
    gpio_set_level(motor2, 0);
    gpio_set_level(motor3, 0);
    gpio_set_level(motor4, 1);
}

void step3(void)
{
    gpio_set_level(motor1, 0);
    gpio_set_level(motor2, 1);
    gpio_set_level(motor3, 0);
    gpio_set_level(motor4, 1);
}

void step4(void)
{
    gpio_set_level(motor1, 0);
    gpio_set_level(motor2, 1);
    gpio_set_level(motor3, 1);
    gpio_set_level(motor4, 0);
}

void motor_open(void)
{
    for(int i = 0; i<3; i++)
    {
        step1();
        vTaskDelay(10);
        step2();
        vTaskDelay(10);
        step3();
        vTaskDelay(10);
        step4();
        vTaskDelay(10);
    }
}

void motor_close(void)
{
    for(int i = 0; i<3; i++)
    {
        step4();
        vTaskDelay(10);
        step3();
        vTaskDelay(10);
        step2();
        vTaskDelay(10);
        step1();
        vTaskDelay(10);
    }
}

int check_mac(uint8_t* mac)
{   
    int check = 0;
    esp_vfs_spiffs_conf_t config = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true,
  };
  esp_vfs_spiffs_register(&config);

  FILE *file = fopen("/spiffs/white_list.txt", "r");
  if(file ==NULL)
  {
    ESP_LOGE(TAG,"File does not exist!");
  }
  else 
  {
    char line[256];
    while(fgets(line, sizeof(line), file) != NULL)
    {
        uint8_t read_mac[6];
        if(sscanf(line, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &read_mac[0], &read_mac[1], &read_mac[2], &read_mac[3], &read_mac[4], &read_mac[5]) != 6)
        {
            printf("Mac leida no valida\n");
            return -1;
        }
        int correcto = 1;
        for(int i = 0; i < 6; i++)
        {
            if(read_mac[i] != mac[i])
            {
                correcto = 0;
            }
        }
        printf("\n");
        if(correcto == 1)
        {
            check = 1;
        }
    }
    fclose(file);
  }
  esp_vfs_spiffs_unregister(NULL);
  return check;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        if(check_mac(event->mac) == 1)
        {
            acceso++;
            ESP_LOGI(TAG, "Acceso correcto");
        }
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        if(check_mac(event->mac) == 1)
        {
            acceso--;
            ESP_LOGI(TAG, "Cliente desconectado");
        }
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif
            .pmf_cfg = {
                    .required = true,
            },
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

// Página principal con cuatro botones
static esp_err_t root_get_handler(httpd_req_t *req) {
    const char *html_page = "<!DOCTYPE html>"
                            "<html>"
                            "<head><title>Control de Acciones</title></head>"
                            "<body>"
                            "<h1>Control de Puerta</h1>"
                            "<form action=\"/abrir\" method=\"POST\">"
                            "<button type=\"submit\">Abrir</button>"
                            "</form>"
                            "<form action=\"/cerrar\" method=\"POST\">"
                            "<button type=\"submit\">Cerrar</button>"
                            "</form>"
                            "<form action=\"/bloquear\" method=\"POST\">"
                            "<button type=\"submit\">Bloquear</button>"
                            "</form>"
                            "<form action=\"/desbloquear\" method=\"POST\">"
                            "<button type=\"submit\">Desbloquear</button>"
                            "</form>"
                            "</body>"
                            "</html>";
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Manejador para el botón "Abrir"
static esp_err_t abrir_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Acción: Abrir");
    httpd_resp_send(req, "Puerta abierta", HTTPD_RESP_USE_STRLEN);
    if(puerta_abierta == 0 && acceso == 1 && bloqueada == 0 && bt_check >=1)
    {
        //Funcion abrir puerta
        puerta_abierta = 1;
        motor_open();
        gpio_set_level(writeDoorPin, 1);
        servidor = true;
    }
    return ESP_OK;
}

// Manejador para el botón "Cerrar"
static esp_err_t cerrar_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Acción: Cerrar");
    httpd_resp_send(req, "Puerta cerrada", HTTPD_RESP_USE_STRLEN);
    if(puerta_abierta == 1 && acceso == 1 && bloqueada == 0 && bt_check >=1)
    {
        //Funcion cerrar puerta
        puerta_abierta = 0;
        motor_close();
        gpio_set_level(writeDoorPin, 0);
        servidor = true;
    }
    return ESP_OK;
}

// Manejador para el botón "Bloquear"
static esp_err_t bloquear_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Acción: Bloquear");
    httpd_resp_send(req, "Puerta bloqueada", HTTPD_RESP_USE_STRLEN);
    if(puerta_abierta == 0 && acceso >= 1 && bt_check >=1)
    {
        //Funcion blouear puerta
        bloqueada = 1;
        open_door = 0;
        gpio_set_level(pin1, open_door);
        gpio_set_level(writeLockPin, 1);
        servidor = true;
    }
    return ESP_OK;
}

// Manejador para el botón "Desbloquear"
static esp_err_t desbloquear_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Acción: Desbloquear");
    httpd_resp_send(req, "Puerta desbloqueada", HTTPD_RESP_USE_STRLEN);
    if(puerta_abierta == 0 && acceso >= 1 && bt_check >=1)
    {
        //Funcion desblquear puerta
        bloqueada = 0;
        open_door = 1;
        gpio_set_level(pin1, open_door);
        gpio_set_level(writeLockPin, 0);
        servidor = true;
    }
    return ESP_OK;
}

// Iniciar el servidor web
httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Ruta para la página principal
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // Ruta para la acción "Abrir"
        httpd_uri_t abrir_uri = {
            .uri       = "/abrir",
            .method    = HTTP_POST,
            .handler   = abrir_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &abrir_uri);

        // Ruta para la acción "Cerrar"
        httpd_uri_t cerrar_uri = {
            .uri       = "/cerrar",
            .method    = HTTP_POST,
            .handler   = cerrar_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &cerrar_uri);

        // Ruta para la acción "Bloquear"
        httpd_uri_t bloquear_uri = {
            .uri       = "/bloquear",
            .method    = HTTP_POST,
            .handler   = bloquear_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &bloquear_uri);

        // Ruta para la acción "Desbloquear"
        httpd_uri_t desbloquear_uri = {
            .uri       = "/desbloquear",
            .method    = HTTP_POST,
            .handler   = desbloquear_post_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &desbloquear_uri);

        ESP_LOGI(TAG, "Servidor web iniciado");
    }

    return server;
}

void app_main(void)
{
    set_pins();
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
    start_webserver();
    nimble_port_init();                        // 3 - Initialize the host stack
    ble_svc_gap_device_name_set("Cerradura"); // 4 - Initialize NimBLE configuration - server name
    ble_svc_gap_init();                        // 4 - Initialize NimBLE configuration - gap service
    ble_svc_gatt_init();                       // 4 - Initialize NimBLE configuration - gatt service
    ble_gatts_count_cfg(gatt_svcs);            // 4 - Initialize NimBLE configuration - config gatt services
    ble_gatts_add_svcs(gatt_svcs);             // 4 - Initialize NimBLE configuration - queues gatt services.
    ble_hs_cfg.sync_cb = ble_app_on_sync;      // 5 - Initialize application
    nimble_port_freertos_init(host_task);      // 6 - Run the thread
    int lock, door;
    int ant_lock = -1;
    int ant_door = -1;
    while(1)
    {
        if(acceso>0)
        {
            ESP_LOGI(TAG, "Usuario verificado");
            printf("Puerta: %d, Acceso: %d, Bloqueada: %d\n", puerta_abierta, acceso, bloqueada);
        }
        lock = gpio_get_level(readLockPin);
        door = gpio_get_level(readDoorPin);
        if(!servidor)
        {
            if(lock != ant_lock)
            {
                bloqueada = lock;
                if(lock == 1)
                {
                    gpio_set_level(pin1, 0);
                }
                else
                {
                    gpio_set_level(pin1, 1);
                }
            }
            if(door != ant_door)
            {
                if(door == 1)
                {
                    puerta_abierta = 1;
                    motor_open();
                }
                else
                {
                    puerta_abierta = 0;
                    motor_close();
                }
            }

            ant_door = door;
            ant_lock = lock;
        }
        else{
            servidor = false;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
}
