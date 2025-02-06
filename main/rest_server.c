/* HTTP Restful API Server

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <bme680.h>

bme680_t sensor;
bme680_values_float_t values;
uint32_t duration;
unsigned char mac[6];
#define mapRange(a1,a2,b1,b2,s) (b1 + (s-a1)*(b2-b1)/(a2-a1))

#define BME680_ADDRESS 0x76
#define ADC1_EXAMPLE_CHAN0  ADC1_CHANNEL_7
#define ADC_EXAMPLE_ATTEN           ADC_ATTEN_DB_11
#define DATA_LENGTH 8

static const char *REST_TAG = "esp-rest";
#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

void init_bme680(void)
{
    memset(&sensor, 0, sizeof(bme680_t));

    ESP_ERROR_CHECK(bme680_init_desc(&sensor, BME680_ADDRESS, 0, 21, 22));

    // init the sensor
    ESP_ERROR_CHECK(bme680_init_sensor(&sensor));

    // Changes the oversampling rates to 4x oversampling for temperature
    // and 2x oversampling for humidity. Pressure measurement is skipped.
    bme680_set_oversampling_rates(&sensor, BME680_OSR_4X, BME680_OSR_2X, BME680_OSR_2X);

    // Change the IIR filter size for temperature and pressure to 7.
    bme680_set_filter_size(&sensor, BME680_IIR_SIZE_7);

    // Change the heater profile 0 to 200 degree Celsius for 100 ms.
    bme680_set_heater_profile(&sensor, 0, 200, 100);
    bme680_use_heater_profile(&sensor, 0);

    // Set ambient temperature to 10 degree Celsius
    bme680_set_ambient_temperature(&sensor, 10);

    // as long as sensor configuration isn't changed, duration is constant
    bme680_get_measurement_duration(&sensor, &duration);

}


void get_bme680_readings(void)
{
    
    // trigger the sensor to start one TPHG measurement cycle
    if (bme680_force_measurement(&sensor) == ESP_OK)
    {
        // passive waiting until measurement results are available
        vTaskDelay(duration);

        // get the results and do something with them
        if (bme680_get_results_float(&sensor, &values) == ESP_OK)
            printf("BME680 Sensor: %.2f °C, %.2f %%, %.2f hPa, %.2f KOhm\n",
                   values.temperature, values.humidity, values.pressure, values.gas_resistance);
    }
}

/* Simple handler for getting system handler */
static esp_err_t system_info_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Simple handler for getting system handler */
static esp_err_t bme_exporter(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    char *mac_string = (char*)malloc(30 * sizeof(char));
    sprintf(mac_string, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    get_bme680_readings();
    

    char *metric = (char*)malloc(1000 * sizeof(char));
    char *metric_buffer = (char*)malloc(100 * sizeof(char));

    sprintf(metric, "esp32_bme680_tempreature{deviceid=\"%s\"} %f\n", mac_string, values.temperature);
    sprintf(metric_buffer, "esp32_bme680_humidity{deviceid=\"%s\"} %f\n", mac_string, values.humidity);
    strcat(metric, metric_buffer);
    sprintf(metric_buffer, "esp32_bme680_gas_resistance{deviceid=\"%s\"} %f\n", mac_string, values.gas_resistance);
    strcat(metric, metric_buffer);
    sprintf(metric_buffer, "esp32_bme680_pressure{deviceid=\"%s\"} %f\n", mac_string, values.pressure);
    strcat(metric, metric_buffer);

    httpd_resp_sendstr(req, metric);
    
    free((void *)metric);
    free((void *)mac_string);
    free((void *)metric_buffer);
    return ESP_OK;
}

esp_err_t start_rest_server()
{
    ESP_ERROR_CHECK(i2cdev_init());
    init_bme680();
    esp_efuse_mac_get_default(mac);

    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_uri = {
        .uri = "/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_info_get_uri);

    httpd_uri_t metrics_get_uri = {
        .uri = "/metrics",
        .method = HTTP_GET,
        .handler = bme_exporter,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &metrics_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
