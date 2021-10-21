#include "http_ota_upgrade.h"
#include "http_ota_internal.h"

#include "esp_http_client.h"
#include <esp_event_loop.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>

#include <esp_ota_ops.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>


static const char *TAG="WEB";
static const uint32_t MAX_BLOCK_SIZE = 1536;
static const char UPGRADE_ERR_UPGRADE_ALREADY_IN_PROGRESS[] = "Another upgrade is in progress already\n";
static const char UPGRADE_ERR_PARTITION_NOT_FOUND[] = "Failed to detect partition for upgrade\n";
static const char UPGRADE_ERR_FAILED_TO_START[] = "Failed to start OTA\n";
//static const char UPGRADE_ERR_FAILED_TO_WRITE[] = "Failed to write partition\n";
static const char UPGRADE_ERR_VERIFICATION_FAILED[] = "Invalid firmware detected\n";
static void (*s_on_upgrade_start)(void) = NULL;
static void (*s_on_upgrade_end)(bool) = NULL;
static void (*s_on_write_data)(const uint8_t *buffer, int size) = NULL;
static bool s_fw_upgrade_proceed = false;
static bool (*s_validate_cb)(const char *new_version);
static esp_ota_handle_t ota_handle = NULL;


static esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
//            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
//            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
//            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
//            printf("%.*s\n", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
//            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client))
            {
                if (esp_http_client_get_status_code(evt->client) == 200)
                {
                    if (s_on_write_data)
                    {
                        if ( evt->data_len < MAX_BLOCK_SIZE )
                        {
                            ((uint8_t *)evt->data)[evt->data_len] = 0;
                        }
                        s_on_write_data( evt->data, evt->data_len );
                    }
                }
//                printf("%.*s\n", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
//            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
//            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void on_write_data(const uint8_t *buffer, int size)
{
    if ( esp_ota_write(ota_handle, buffer, size) != ESP_OK)
    {
//        error_msg = UPGRADE_ERR_FAILED_TO_WRITE;
//        goto error;
    }
}

void on_check_firmware(const uint8_t *buffer, int size)
{
    if ( s_validate_cb )
    {
        s_fw_upgrade_proceed = s_validate_cb((const char *)buffer);
    }
    else
    {
        s_fw_upgrade_proceed = false;
    }
}

bool http_get_file_perform(const char *link, void (*cb)(const uint8_t *buffer, int size) )
{
    s_on_write_data = cb;
    esp_http_client_config_t config =
    {
       .url = link,
       .event_handler = _http_event_handle,
       .buffer_size = MAX_BLOCK_SIZE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
//    esp_http_client_set_header(client, "Range", "bytes:0-1023");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Status = %d, content_length = %d",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
//        int len = esp_http_client_read(client, (char *)content, MAX_BLOCK_SIZE);
//        printf("HHHHHH: %.*s", len, (char*)content);
    }
    else
    {
        ESP_LOGE( TAG, "Failed to perform HTTP GET request from %s", link );
    }
    esp_http_client_cleanup(client);
    return err == ESP_OK;
}

bool http_get_file_read(const char *link, uint8_t *buffer, int max_size)
{
    // TODO: Waiting for esp_http_client_follow_redirection
    esp_http_client_config_t config =
    {
       .url = link, //"http://httpbin.org/redirect/2",
       .event_handler = _http_event_handle,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
//    esp_http_client_set_url(client, bin_link);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Range", "bytes:0-1023");
    esp_err_t err = ESP_OK;
    do {
    esp_err_t err = esp_http_client_open(client, 0);
    if ( err != ESP_OK )
    {
        return false;
    }
    int content_length = esp_http_client_fetch_headers(client);
    int read_len = esp_http_client_read(client, (char *)buffer, content_length > max_size ? max_size : content_length);
    if (read_len <= 0)
    {
        ESP_LOGE(TAG, "Error read data");
        return false;
    }
    ESP_LOGI(TAG, "HTTP read status = %d, content_length = %d",
                    esp_http_client_get_status_code(client),
                    esp_http_client_get_content_length(client));
//    printf("HHHHHH: %.*s", read_len, (char*)buffer);
    if ( esp_http_client_get_status_code(client) != 302 )
    {
        break;
    }
    esp_http_client_close(client);
    } while (1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK;
}


static const char *s_ver_link;
static const char *s_bin_link;

static void upgrade_task(void *pvParameters)
{
    /* Read request content */
    const char* error_msg = NULL;
    const esp_partition_t* next_partition = NULL;
    uint8_t *content = malloc(MAX_BLOCK_SIZE);
    // This is delay to workaround socket issues
    vTaskDelay( 2000 / portTICK_PERIOD_MS );
    bool locked = false;
    if ( !http_ota_try_lock() )
    {
        error_msg = UPGRADE_ERR_UPGRADE_ALREADY_IN_PROGRESS;
        goto not_needed;
    }
    locked = true;
    if ( !content )
    {
        error_msg = UPGRADE_ERR_FAILED_TO_START;
        goto error;
    }
    s_fw_upgrade_proceed = false;
    if ( s_ver_link )
    {
        ESP_LOGI( TAG, "Validating version per %s", s_ver_link);
        if ( !http_get_file_perform( s_ver_link, on_check_firmware ) )
        {
            error_msg = UPGRADE_ERR_FAILED_TO_START;
            goto not_needed;
        }
        if ( s_fw_upgrade_proceed )
        {
            ESP_LOGI( TAG, "Upgrade confirmed" );
        }
        else
        {
            ESP_LOGI( TAG, "Upgrade declined by version file verification" );
        }
    }
    else
    {
        s_fw_upgrade_proceed = true;
    }
    if (!s_fw_upgrade_proceed)
    {
        goto not_needed;
    }
    if ( s_on_upgrade_start )
    {
        s_on_upgrade_start();
    }

    /* Truncate if content length larger than the buffer */
//    size_t total_size = 0;

    const esp_partition_t* active_partition = esp_ota_get_running_partition();
    next_partition = esp_ota_get_next_update_partition(active_partition);
    if (!next_partition)
    {
        error_msg = UPGRADE_ERR_PARTITION_NOT_FOUND;
        goto error;
    }
    esp_err_t err = esp_ota_begin(next_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK)
    {
        error_msg = UPGRADE_ERR_FAILED_TO_START;
        goto error;
    }

//    return http_get_file_read( bin_link, content, MAX_BLOCK_SIZE );
    ESP_LOGI( TAG, "Upgrading from %s", s_bin_link);
    http_get_file_perform( s_bin_link, on_write_data );
/*    while (total_size > 0)
    {
        // TODO: Read
        size_t recv_size = MAX_BLOCK_SIZE;
        int ret = httpd_req_recv(req, (char *)content, recv_size);
        if (ret < 0)
        {
            esp_ota_end(ota_handle);
            return ESP_FAIL;
        }
        total_size -= ret;
        if ( esp_ota_write(ota_handle, content, ret) != ESP_OK)
        {
            error_msg = UPGRADE_ERR_FAILED_TO_WRITE;
            goto error;
        }
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, content, ret, ESP_LOG_DEBUG);
        taskYIELD();
    }*/
    if ( esp_ota_end(ota_handle) != ESP_OK )
    {
        ota_handle = 0;
        error_msg = UPGRADE_ERR_VERIFICATION_FAILED;
        goto error;
    }
    if ( s_on_upgrade_end )
    {
        s_on_upgrade_end( true );
    }
    ESP_LOGI(TAG, "Upgrade successful");
    esp_ota_set_boot_partition(next_partition);
    ota_handle = 0;
    free( content );
    fflush(stdout);
    /** Delay before reboot */
    vTaskDelay( 4000 / portTICK_PERIOD_MS );
    http_ota_unlock();
    esp_restart();
    vTaskDelete( NULL );
    /* We never go to this place */

not_needed:

    if ( content )
    {
        free( content );
    }
    if ( locked )
    {
        http_ota_unlock();
    }
    vTaskDelete( NULL );

error:
    if ( content )
    {
        free( content );
    }
    if ( s_on_upgrade_end )
    {
        s_on_upgrade_end( false );
    }
    if (!ota_handle)
    {
        esp_ota_end(ota_handle);
    }
    esp_log_write( ESP_LOG_ERROR, TAG, error_msg );
    if ( locked )
    {
        http_ota_unlock();
    }
    vTaskDelete( NULL );
}

void http_client_ota_upgrade( const char *version_link, const char *firmware_link,
                              bool (*validate_cb)(const char *new_version),
                              void (*on_upgrade_start)(void),
                              void (*on_upgrade_end)(bool success) )
{
    s_ver_link = version_link;
    s_bin_link = firmware_link;
    s_validate_cb = validate_cb;
    s_on_upgrade_start = on_upgrade_start;
    s_on_upgrade_end = on_upgrade_end;

    xTaskCreate(&upgrade_task,
                "upgrade_task",
                4096, NULL, 3, NULL);
}


