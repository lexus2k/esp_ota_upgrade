#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool http_ota_try_lock(void);
void http_ota_unlock(void);

#ifdef __cplusplus
}
#endif
