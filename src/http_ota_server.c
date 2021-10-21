#include "http_ota_upgrade.h"
#include "http_ota_internal.h"

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
static const char UPGRADE_ERR_FAILED_TO_WRITE[] = "Failed to write partition\n";
static const char UPGRADE_ERR_VERIFICATION_FAILED[] = "Invalid firmware detected\n";
static bool (*s_guard)(httpd_req_t *req) = NULL;
static void (*s_on_upgrade_start)(void) = NULL;
static void (*s_on_upgrade_end)(bool) = NULL;

/* Our URI handler function to be called during POST /uri request */
static esp_err_t fw_update_callback(httpd_req_t *req)
{
    if ( s_guard && !s_guard(req) )
    {
        return ESP_OK;
    }
    const char* error_msg = NULL;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t* next_partition = NULL;
    uint8_t *content = malloc(MAX_BLOCK_SIZE);
    bool locked = false;
    if ( !http_ota_try_lock() )
    {
        error_msg = UPGRADE_ERR_UPGRADE_ALREADY_IN_PROGRESS;
        goto error;
    }
    locked = true;
    if ( !content )
    {
        error_msg = UPGRADE_ERR_FAILED_TO_START;
        goto error;
    }

    if ( s_on_upgrade_start )
    {
        s_on_upgrade_start();
    }
    /* Read request content */
    /* Truncate if content length larger than the buffer */
    size_t total_size = req->content_len;

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

    while (total_size > 0)
    {
        size_t recv_size = MAX_BLOCK_SIZE;
        int ret = httpd_req_recv(req, (char *)content, recv_size);
        if (ret < 0)
        {
            goto internal_error;
        }
        total_size -= ret;
        if ( esp_ota_write(ota_handle, content, ret) != ESP_OK)
        {
            error_msg = UPGRADE_ERR_FAILED_TO_WRITE;
            goto error;
        }
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, content, ret, ESP_LOG_DEBUG);
        taskYIELD();
    }
    if ( esp_ota_end(ota_handle) != ESP_OK )
    {
        error_msg = UPGRADE_ERR_VERIFICATION_FAILED;
        goto error;
    }
    if ( s_on_upgrade_end )
    {
        s_on_upgrade_end( true );
    }
    const char resp[]="SUCCESS";
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_send(req, resp, sizeof(resp));
    ESP_LOGI(TAG, "Upgrade successful");
    esp_ota_set_boot_partition(next_partition);
    ota_handle = 0;
    free( content );
    fflush(stdout);
    /** Delay before reboot */
    vTaskDelay( 4000 / portTICK_PERIOD_MS );
    http_ota_unlock();
    esp_restart();
    /* We never go to this place */
    return ESP_OK;
internal_error:

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
    if ( locked )
    {
        http_ota_unlock();
    }
    esp_log_write( ESP_LOG_ERROR, TAG, "HTTP receive error" );
    /* In case of recv error, returning ESP_FAIL will
     * ensure that the underlying socket is closed */
    return ESP_FAIL;

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
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, error_msg, strlen(error_msg));
    if ( locked )
    {
        http_ota_unlock();
    }
    return ESP_OK;
}

static httpd_uri_t uri_update = {
    .uri      = "/fwupdate",
    .method   = HTTP_POST,
    .handler  = fw_update_callback,
    .user_ctx = NULL
};


void register_httpd_ota_handler( httpd_handle_t server,
                           bool (*guard)(httpd_req_t *req),
                           void (*on_upgrade_start)(void),
                           void (*on_upgrade_end)(bool success) )
{
    s_guard = guard;
    s_on_upgrade_start = on_upgrade_start;
    s_on_upgrade_end = on_upgrade_end;
    httpd_register_uri_handler(server, &uri_update);
}

