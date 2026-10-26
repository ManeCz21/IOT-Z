#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define HA_SENSOR_ENDPOINT 1
static const char *TAG = "ZB_ROUTER";

/* Tarea que lee el sensor local y envia el reporte al Coordinador */
static void sensor_report_task(void *pvParameters)
{
    while (1) {
        int16_t temp_val = 2450; // Ejemplo: 24.50 C
        uint16_t hum_val = 5500; // Ejemplo: 55.00 %

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(HA_SENSOR_ENDPOINT,
            ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &temp_val, false);

        esp_zb_zcl_set_attribute_val(HA_SENSOR_ENDPOINT,
            ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &hum_val, false);
        esp_zb_lock_release();

        ESP_LOGI(TAG, "Router reportando -> Temp: %.2f C | Hum: %.2f %%", temp_val/100.0f, hum_val/100.0f);
        vTaskDelay(pdMS_TO_TICKS(10000)); // Reportar cada 10 segundos
    }
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
            ESP_LOGI(TAG, "Router unido a la red. Funcion repetidora activa.");
            /* Abrir red localmente para permitir que los ZED se unan a traves de este Router */
            esp_zb_bdb_open_network(180);
            xTaskCreate(sensor_report_task, "sensor_task", 4096, NULL, 4, NULL);
        } else {
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    /* Crear Endpoint con cluster de sensor de temperatura y anadir humedad */
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
    xTaskCreate(esp_zb_task, "Zigbee_ZR", 4096, NULL, 5, NULL);
}
