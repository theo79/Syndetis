// Compile the actual HTTP serializer with SDK I/O stubs, not a rewritten serializer.
#include <stdio.h>
#include <string.h>
#define asm(x) /* Embedded asset aliases are not needed by this host check. */
#include "../main/dashboard.c"
#undef asm
static neighbor_table_t fixture;
static const char *response;
static int response_length;
const unsigned char dashboard_html_start[2] = {0}, dashboard_html_end[2] = {0};

bool discovery_snapshot(neighbor_table_t *out) { *out=fixture; return true; }
app_status_t status_get(void) { app_status_t result={0}; result.ethernet.state=ETH_READY; return result; }
const char *status_ethernet_state_name(ethernet_state_t state) { (void)state; return "Ready"; }
esp_err_t httpd_resp_send(httpd_req_t *r,const char *body,int n) {
    (void)r;response=body;response_length=n;return 0;
}
static void fill(char *s,size_t n,char c) { memset(s,c,n-1);s[n-1]=0; }
static int emit(void)
{
    httpd_req_t request={0};
    if(status_get_handler(&request)!=ESP_OK||response_length<=0||response_length>=16384)return 1;
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
    if(emit())return 1;
    fixture.records[0].fields=FIELD_CHASSIS|FIELD_PORT;
    fixture.records[1].name[0]=0;
    strcpy(fixture.records[2].description,"<script>\"quoted\"\\line\n</script>");
    return emit();
}
