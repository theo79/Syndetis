#pragma once

// Prototype WPA2-Personal password. Change here before distributing firmware.
// Never logged, persisted as an enabled flag, or sent through the status API.
#define WEB_AP_PASSWORD "Syndetis"
#define WEB_AP_IP "192.168.4.1"
#define WEB_URL "http://192.168.4.1/"

_Static_assert(sizeof(WEB_AP_PASSWORD) - 1 >= 8 && sizeof(WEB_AP_PASSWORD) - 1 <= 63,
               "WPA2 passphrase must contain 8 to 63 characters");
