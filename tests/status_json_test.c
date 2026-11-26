// Compile the actual HTTP serializer with SDK I/O stubs, not a rewritten serializer.
#include <stdio.h>
#include <string.h>
#define asm(x) /* Embedded asset aliases are not needed by this host check. */
#include "../main/dashboard.c"
#undef asm
static neighbor_table_t fixture;
static dhcp_status_t dhcp_fixture;
static int dhcp_requests;
static bool request_ok=true;
dhcp_status_t dhcp_test_status(void){return dhcp_fixture;}
const char *dhcp_state_name(dhcp_state_t s){(void)s;return "IPv4 off / passive";}
bool dhcp_test_request(bool start){(void)start;++dhcp_requests;return request_ok;}
bool dashboard_hotspot_request(httpd_req_t*r){return !r->denied;}
static const char *response;
static int response_length;
static char chunks[60000];
static dcp_inventory_t scan_fixture;
dcp_summary_t profinet_summary(void) { return scan_fixture.summary; }
void profinet_snapshot(dcp_inventory_t *out) { *out = scan_fixture; }
const unsigned char dashboard_html_start[2] = {0}, dashboard_html_end[2] = {0};

bool discovery_snapshot(neighbor_table_t *out) { *out=fixture; return true; }
app_status_t status_get(void) { app_status_t result={0}; result.ethernet.state=ETH_READY; return result; }
const char *status_ethernet_state_name(ethernet_state_t state) { (void)state; return "Ready"; }
esp_err_t httpd_resp_send(httpd_req_t *r,const char *body,int n) {
    (void)r;response=body;response_length=n;return 0;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *body,size_t n) {
    (void)r;
    if (!body) return ESP_OK;
    if (n > sizeof(chunks) - response_length) return ESP_FAIL;
    memcpy(chunks + response_length, body, n);
    response = chunks;
    response_length += (int)n;
    return ESP_OK;
}
static void fill(char *s,size_t n,char c) { memset(s,c,n-1);s[n-1]=0; }
static int emit(void)
{
    httpd_req_t request={0};
    response_length=0;
    if(status_get_handler(&request)!=ESP_OK||response_length<=0||response_length>=(int)sizeof(chunks))return 1;
    fwrite(response,1,response_length,stdout);putchar('\n');return 0;
}
int main(void)
{
    fixture.link_up=true;fixture.ever_received=true;
    for(int i=0;i<NEIGHBOR_CAPACITY;i++) {
        neighbor_t *r=&fixture.records[i];
        r->used=true;r->fields=63;r->protocol=PROTOCOL_LLDP;r->ttl=120;r->received_ms=1000;
        r->source[0]=2;r->source[5]=i+1;r->vlan=100;r->management_kind=1;
        fill(r->name,sizeof(r->name),'"');fill(r->chassis,sizeof(r->chassis),'\\');
        fill(r->port,sizeof(r->port),'"');fill(r->description,sizeof(r->description),'\\');
        fill(r->management,sizeof(r->management),'"'); // Pessimistic bound, beyond a real IP's escaping needs.
    }
    dhcp_fixture.error=ESP_FAIL;strcpy(dhcp_fixture.error_step,"primary API");
    dhcp_fixture.cleanup_error=true;dhcp_fixture.cleanup_code=0x5001;
    strcpy(dhcp_fixture.cleanup_step,"cleanup \"API\"");
    dcp_begin(&scan_fixture, 123, 1000);
    for (unsigned i = 0; i < DCP_CAPACITY; ++i) {
        dcp_device_t device = {
            .mac = {2,0,0,0,2,i}, .fields = DCP_NAME|DCP_VENDOR|DCP_IP|DCP_ROLE|DCP_ID,
            .ip = {192,0,2,2}, .mask = {255,255,255,0}, .gateway = {192,0,2,1},
            .vendor_id = 1024, .device_id = 4096, .role = 3, .ip_info = 0x81
        };
        fill(device.name,sizeof(device.name),'"');
        fill(device.vendor,sizeof(device.vendor),'\\');
        dcp_observe(&scan_fixture,&device);
    }
    dcp_finish(&scan_fixture.summary, DCP_COMPLETE, 1500);
    if(emit())return 1;
    fixture.records[0].fields=FIELD_CHASSIS|FIELD_PORT;
    fixture.records[1].name[0]=0;
    strcpy(fixture.records[2].description,"<script>\"quoted\"\\line\n</script>");
    scan_fixture.devices[0].fields = DCP_NAME;
    strcpy(scan_fixture.devices[0].name, "<img src=x>");
    dcp_link_down(&scan_fixture.summary, 1800);
    if(emit())return 1;
    dhcp_fixture=(dhcp_status_t){.state=MANUAL_ACTIVE,.active=true,.mode=WIRED_MANUAL,.prefix=24};
    strcpy(dhcp_fixture.ip,"192.168.10.250");strcpy(dhcp_fixture.mask,"255.255.255.0");
    scan_fixture = (dcp_inventory_t){0};
    return emit();
}
