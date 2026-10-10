#include "ui_profinet.h"
#include <stdio.h>
#include <string.h>
static void ip_line(char *row,const char *label,const uint8_t *ip){snprintf(row,22,"%s%u.%u.%u.%u",label,ip[0],ip[1],ip[2],ip[3]);}
void ui_profinet_render(char rows[8][22],unsigned page,unsigned index,unsigned count,const profinet_device_t *d){
    memset(rows,0,8*22);snprintf(rows[0],22,"PN DEVICE %u/%u",index+1,count);
    if(page==0){
        snprintf(rows[1],22,"%.21s",d->flags&PN_NAME?(d->station_name[0]?d->station_name:"Station name empty"):"Name not advertised");
        if(!(d->flags&PN_IP))snprintf(rows[2],22,"IP not advertised");
        else if(!(d->ip[0]|d->ip[1]|d->ip[2]|d->ip[3])||(d->ip_block_info&0x7f)==0)snprintf(rows[2],22,"IP not set");
        else ip_line(rows[2],"",d->ip);
        if(d->flags&PN_IDS){snprintf(rows[3],22,"VID: %04X",d->vendor_id);snprintf(rows[4],22,"DID: %04X",d->device_id);}
        if(d->flags&(PN_NAME_TRUNC|PN_VENDOR_TRUNC|PN_ALIAS_TRUNC))snprintf(rows[5],22,"Text truncated");
    }else if(page==1){
        snprintf(rows[1],22,"%02X:%02X:%02X:%02X:%02X:%02X",d->mac[0],d->mac[1],d->mac[2],d->mac[3],d->mac[4],d->mac[5]);
        if(d->flags&PN_IP){ip_line(rows[2],"IP ",d->ip);ip_line(rows[3],"Mask ",d->subnet);ip_line(rows[4],"GW ",d->gateway);}
        if(d->flags&PN_IP_CONFLICT)snprintf(rows[5],22,"IP conflict");
    }else{
        if(d->flags&PN_ROLE)snprintf(rows[1],22,"Role mask: %02X",d->device_role);
        if(d->flags&PN_INSTANCE)snprintf(rows[2],22,"Instance: %04X",d->device_instance);
        if(d->flags&PN_VENDOR)snprintf(rows[3],22,"%.21s",d->vendor_value);
        if(d->flags&PN_ALIAS)snprintf(rows[4],22,"%.21s",d->alias_name);
        if(d->flags&PN_ROLE)snprintf(rows[5],22,"%s%s%s%s",d->device_role&1?"DEV ":"",d->device_role&2?"CTRL ":"",d->device_role&4?"MULTI ":"",d->device_role&8?"SUP":"");
    }
    snprintf(rows[6],22,"UP/DOWN PAGE/DEVICE");snprintf(rows[7],22,"OK: BACK");
}
