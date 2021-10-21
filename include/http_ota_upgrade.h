#include <stdint.h>
#include <stdbool.h>

#if !defined(IDF_VERSION)
#include "esp_http_server.h"
#elif IDF_VERSION>=030200
#include "esp_http_server.h"
#else
#include "http_server.h"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Registers OTA handler for getting binary file from client
 * HTTP server must be started by Application. This function
 * adds http://ADDR/fwupdate url to process upgrade requests.
 * @param server handle to http server
 * @param guard guard function, that can prevent upgrade
 * @param on_upgrade_start callback to be called, when upgrade is about to start.
 *        Please, note that upgrade cannot be interrupted at this step.
 * @param on_upgrade_end callback to be called after update completes. Callback should
 *        accept boolean value: true if upgrade was successful, false if not.
 * @warning after successful upgrade esp32 will be rebooted in 2 seconds.
 */
void register_httpd_ota_handler( httpd_handle_t server,
                                 bool (*guard)(httpd_req_t *req),
                                 void (*on_upgrade_start)(void),
                                 void (*on_upgrade_end)(bool success) );


/**
 * Starts upgrade in http client mode. In this mode function downloads version_link
 * content to passes it to validate_cb application function for verification. if
 * application confirms upgrade, then upgrade continues.
 *
 * @note function returns immediately, upgrade takes place in separate thread.
 *
 * @param version_link pointer to null-terminated string, containing HTTP link to version file.
 *        Content of version file will be downloaded and passed to validate_cb function.
 *        If NULL is passed, then no version validation is performed.
 * @param firmware_link pointer to null-terminated string, containing HTTP link to firmware
 *        binary.
 * @param validate_cb application callback, which receives version file content as null-terminated
 *        string. Can be NULL if no version validation should be performed.
 * @param on_upgrade_end callback to be called after update completes. Callback should
 *        accept boolean value: true if upgrade was successful, false if not.
 * @warning after successful upgrade esp32 will be rebooted in 2 seconds.
 */
void http_client_ota_upgrade( const char *version_link, const char *firmware_link,
                              bool (*validate_cb)(const char *new_version),
                              void (*on_upgrade_start)(void),
                              void (*on_upgrade_end)(bool success) );

#ifdef __cplusplus
}
#endif
