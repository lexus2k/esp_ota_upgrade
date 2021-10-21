#include "http_ota_internal.h"

//static SemaphoreHandle_t xMutex = NULL;
// TODO: Hack for now
static volatile bool is_locked = false;

bool http_ota_try_lock(void)
{
    if ( is_locked ) return false;
    is_locked = true;
    return true;
}


void http_ota_unlock(void)
{
    is_locked = false;
}
