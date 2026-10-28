#include "ota_http_policy.h"
#include <stdio.h>
#include <string.h>
bool ota_http_authority(uint32_t local,const char *host,const char *origin,bool mutation)
{
    if(!local || !host)return false;
    char authority[24],expected[32];
    snprintf(authority,sizeof(authority),"%u.%u.%u.%u:%u",(unsigned)(local>>24),
        (unsigned)(local>>16&255),(unsigned)(local>>8&255),(unsigned)(local&255),OTA_HTTP_PORT);
    if(strcmp(host,authority))return false;
    snprintf(expected,sizeof(expected),"http://%s",authority);
    return !mutation || (origin && !strcmp(origin,expected));
}
