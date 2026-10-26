#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"

#define HA_SENSOR_ENDPOINT 1
static const char *TAG = "ZB_SLEEPY_ZED";

static void zed_report_cycle(uint8_t param)
{
    int16_t temp_val = 2180; // Ejemplo: 21.80 C (nodo exterior)
    uint16_t hum_val = 6800; // Ejemplo: 68.00 %

    ESP_LOGI(TAG, "ZED despierto: leyendo sensor y transmitiendo por la malla...");
    esp_zb_zcl_set_attribute_val(HA_SENSOR_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temp_val, false);

    esp_zb_zcl_set_attribute_val(HA_SENSOR_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &hum_val, false);

    /* Programar el siguiente despertar en 30 segundos (30000 ms) */
    esp_zb_scheduler_alarm((esp_zb_callback_t)zed_report_cycle, 0, 30000);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "ZED unido a la red (a traves de Router o Coordinador).");
            esp_zb_scheduler_alarm((esp_zb_callback_t)zed_report_cycle, 0, 1000);
        } else {
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 5000);
        }
        break;
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        /* Entrar inmediatamente en modo Sleep para ahorrar bateria */
        esp_zb_sleep_now();
        break;
    default:
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* Habilitar modo Sleepy End Device */
    esp_zb_sleep_enable(true);

    esp_zb_temperature_sensor_cfg_t sensor_cfg = ESP_ZB_DEFAULT_TEMPERATURE_SENSOR_CONFIG();
    esp_zb_ep_list_t *ep_list = esp_zb_temperature_sensor_ep_create(HA_SENSOR_ENDPOINT, &sensor_cfg);
    esp_zb_device_register(ep_list);

    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_MAC_CHANNEL_11);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_ZED", 4096, NULL, 5, NULL);
}
