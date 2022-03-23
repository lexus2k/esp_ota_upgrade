# ESP OTA enhanced upgrade

Improved ESP OTA Upgrade component

# How to use

## How to add FW upgrade function to your web-server on the IoT device

![image](https://user-images.githubusercontent.com/16983230/159657446-a23b0caa-f3f0-4737-b708-0d643c55d1e4.png)


```.cpp


static void on_upgrade_start(void)
{
    // Here you can do anything before upgrade process is started
}

static void on_upgrade_end(bool success)
{
    if ( success )
    {
    }
    else
    {
    }
}

static bool before_upgrade_check(httpd_req_t *req)
{
    // Do, whatever you need to do to make sure that sender is allowed to upgrade IoT device
    uint8_t authenticated = validate_session(req, "POST"); // This is an example function
    if ( !authenticated)
    {
        // Do, whatever you want to do in case Upgrade is cancelled
        redirect_to_login_page(req, false);
        return false;
    }
    return true;
}


...
    // Registers /fwupdate URI
    register_httpd_ota_handler(server, before_upgrade_check, on_upgrade_start, on_upgrade_end );
...


```

## Initiate self-upgrade by the device itself

```.cpp

static void on_upgrade_start(void)
{
    // Here you can do anything before upgrade process is started
}

static void on_upgrade_end(bool success)
{
    if ( success )
    {
    }
    else
    {
    }
}

bool on_validate_version(const char * new_ver)
{
    // Validate here the version
    return true;
}

...
    http_client_ota_upgrade( "https://path/to/version/file.txt", // Can be NULL if you don't have version file with the name
                             "https://path/to/new/firmware.bin",
                              on_validate_version,
                              on_upgrade_start,
                              on_upgrade_end );
...

```
