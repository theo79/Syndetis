#pragma once
#include <stdbool.h>

void services_start(void);
// Called only by the physical OLED navigation. Queue a nonblocking request.
bool services_request_web(bool enable);

bool services_boot_healthy(void);
