// Exercise the actual HTTP handler; no networking or hardware.
#define main serializer_main
#include "status_json_test.c"
#undef main
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static httpd_req_t valid(void){httpd_req_t r={0};r.content_len=2;r.uri="/api/dhcp/start";r.host="192.168.4.1";r.origin="http://192.168.4.1";r.type="application/json";r.body="{}";return r;}
int main(void){
 httpd_req_t r=valid();CHECK(dhcp_post(&r)==0&&r.status==202&&dhcp_requests==1);
 r=valid();r.uri="/api/dhcp/stop";CHECK(dhcp_post(&r)==0&&r.status==202&&dhcp_requests==2);
 request_ok=false;r=valid();dhcp_post(&r);CHECK(r.status==409);request_ok=true;
 int calls=dhcp_requests;
 r=valid();r.origin="https://evil.test";CHECK(dhcp_post(&r)==403);
 r=valid();r.origin=NULL;CHECK(dhcp_post(&r)==403);
 r=valid();r.host="evil.test";CHECK(dhcp_post(&r)==403);
 r=valid();r.type="text/plain";CHECK(dhcp_post(&r)==403);
 r=valid();r.content_len=4096;CHECK(dhcp_post(&r)==403);
 r=valid();r.body="[]";CHECK(dhcp_post(&r)==400);
 r=valid();r.body=NULL;CHECK(dhcp_post(&r)==400);
 r=valid();r.denied=true;CHECK(dhcp_post(&r)==403&&index_get(&r)==403&&status_get_handler(&r)==403);
 CHECK(dhcp_requests==calls);
 puts("PASS: actual HTTP actions: prompt 202 acceptance/409 conflicts, bounded body, partial reads, exact JSON, Host/Origin/content-type validation and hotspot guard on all endpoints");
 return 0;
}
