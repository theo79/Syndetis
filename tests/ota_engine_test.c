#include "ota_engine.h"
#include "ota_boot_policy.h"
#include "ota_http_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static ota_engine_t e;
static unsigned begins,writes,finishes,activates,aborts;
static int error;
static size_t flashed;
static uint8_t binary[640];
static bool begin(void *c,size_t n){(void)c;CHECK(n==640);++begins;return error!=1;}
static bool write_data(void*c,const uint8_t*p,size_t n){(void)c;CHECK(p&&n);++writes;flashed+=n;CHECK(flashed<=640);return error!=2;}
static bool finish(void*c){(void)c;++finishes;return error!=3;}
static bool activate(void*c){(void)c;++activates;CHECK(finishes==1);return error!=4;}
static void abort_data(void*c){(void)c;++aborts;}
static void put32(uint8_t*p,unsigned n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void reset(void){
 begins=writes=finishes=activates=aborts=0;flashed=0;error=0;
 ota_io_t io={begin,write_data,finish,activate,abort_data,NULL};
 ota_engine_init(&e,&io,"0.5.0");
 memset(binary,0,sizeof(binary));binary[0]=0xe9;binary[1]=4;binary[23]=1;
 put32(binary+28,256);put32(binary+32,0xabcd5432);
 strcpy((char*)binary+48,"0.5.1");strcpy((char*)binary+80,"Syndetis");
 CHECK(ota_engine_authorize(&e,1024,10));
}
static void start(void){CHECK(ota_engine_begin(&e,sizeof(binary),20));}
static void feed(void){CHECK(ota_engine_feed(&e,binary,sizeof(binary),30));}
int main(void){
 for(unsigned local=0;local<2;++local){
  uint32_t ip=local?0xc000020a:0xc0a80401;
  const char *host=local?"192.0.2.10:8080":"192.168.4.1:8080";
  const char *origin=local?"http://192.0.2.10:8080":"http://192.168.4.1:8080";
  CHECK(ota_http_authority(ip,host,origin,true));
  CHECK(!ota_http_authority(ip,host,"https://192.0.2.10:8080",true));
  CHECK(!ota_http_authority(ip,host,"http://evil.test:8080",true));
  CHECK(!ota_http_authority(ip,host,"null",true));
  CHECK(!ota_http_authority(ip,host,NULL,true));
  const char *bad[]={"192.0.2.99:8080","192.0.2.10","192.0.2.10:80","192.0.2.10:443",
   "syndetis-ota.test:8080","192.0.2.10:8080.evil.test","192.0.2.10:8080/path","",NULL};
  for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)CHECK(!ota_http_authority(ip,bad[i],origin,true));
 }
 CHECK(!ota_http_authority(0,"192.0.2.10:8080","http://192.0.2.10:8080",true));

 // Arbitrary HTTP fragment boundaries; only image bytes reach flash.
 for(size_t chunk=1;chunk<=769;chunk+=13){
  reset();start();CHECK(!ota_engine_begin(&e,sizeof(binary),21));
  for(size_t i=0;i<sizeof(binary);){size_t n=sizeof(binary)-i;if(n>chunk)n=chunk;CHECK(ota_engine_feed(&e,binary+i,n,30+i));i+=n;}
  CHECK(ota_engine_finish(&e,1000)&&e.state==OTA_READY&&flashed==640&&begins==1&&activates==1&&aborts==0);
  CHECK(!ota_engine_begin(&e,sizeof(binary),1001));
  ota_engine_cancel(&e,"late");CHECK(e.state==OTA_READY);
 }
 CHECK(ota_version_newer("1.0.0","0.99.9"));CHECK(!ota_version_newer("0.5.0","0.5.0"));
 CHECK(!ota_version_newer("0.4.99","0.5.0"));CHECK(!ota_version_newer("99999999.0.0","0.5.0"));
 CHECK(!ota_version_newer("1.2.3junk","0.5.0"));CHECK(!ota_version_newer("","0.5.0"));
 // Before any erase: malformed/product/chip/version rejection.
 for(unsigned which=0;which<6;++which){reset();start();
  if(which==0)binary[0]=0;if(which==1)binary[12]=2;if(which==2)binary[80]='X';
  if(which==5)binary[23]=0;
  if(which==3)strcpy((char*)binary+48,"0.5.0");if(which==4)memset(binary+48,'1',32);
  CHECK(!ota_engine_feed(&e,binary,sizeof(binary),30)&&begins==0&&activates==0&&e.state==OTA_FAILED);
 }
 reset();CHECK(!ota_engine_begin(&e,1025,20)&&begins==0);
 reset();CHECK(!ota_engine_begin(&e,200,20)&&begins==0);
 reset();e.state=OTA_IDLE;CHECK(!ota_engine_session_valid(&e,20));CHECK(!ota_engine_begin(&e,sizeof(binary),20));
 reset();CHECK(ota_engine_session_valid(&e,20));ota_engine_cancel(&e,"Physical cancel");
 CHECK(!ota_engine_session_valid(&e,21)&&!ota_engine_begin(&e,sizeof(binary),21));
 reset();ota_engine_tick(&e,10+OTA_AUTHORIZE_MS);CHECK(e.state==OTA_FAILED);
 reset();start();CHECK(ota_engine_feed(&e,binary,300,30));ota_engine_tick(&e,30+OTA_STALL_MS);CHECK(e.state==OTA_FAILED&&aborts==1&&activates==0);
 reset();start();CHECK(ota_engine_feed(&e,binary,300,30));CHECK(!ota_engine_finish(&e,31)&&aborts==1&&activates==0);
 reset();start();CHECK(ota_engine_feed(&e,binary,300,30));ota_engine_cancel(&e,"Network lost");CHECK(aborts==1&&activates==0&&e.state==OTA_CANCELLED);
 CHECK(!ota_engine_feed(&e,binary,1,31));ota_engine_cancel(&e,"again");CHECK(aborts==1);
 reset();start();CHECK(!ota_engine_feed(&e,binary,sizeof(binary)+1,30)&&begins==0);
 for(int i=1;i<=4;++i){reset();error=i;start();bool ok=ota_engine_feed(&e,binary,sizeof(binary),30);if(ok)CHECK(!ota_engine_finish(&e,31));CHECK(e.state==OTA_FAILED);if(i<4)CHECK(activates==0);}
 reset();start();for(uint64_t t=21;t<20+OTA_UPLOAD_MS;t+=9000){e.last_rx=t;ota_engine_tick(&e,t);CHECK(e.state==OTA_RECEIVING);}e.last_rx=20+OTA_UPLOAD_MS;ota_engine_tick(&e,20+OTA_UPLOAD_MS);CHECK(e.state==OTA_FAILED);
 reset();strcpy(e.current_version,"0.5.1");start();
 CHECK(!ota_engine_feed(&e,binary,sizeof(binary),30)&&!begins&&!activates);
 reset();strcpy((char*)binary+48,"0.4.0");start();
 CHECK(!ota_engine_feed(&e,binary,sizeof(binary),30)&&!begins&&!activates);
 CHECK(ota_boot_action(2000,20,true,true)==OTA_BOOT_WAIT);
 CHECK(ota_boot_action(3001,8,true,true)==OTA_BOOT_CONFIRM);
 CHECK(ota_boot_action(3001,8,false,true)==OTA_BOOT_WAIT);
 CHECK(ota_boot_action(3001,8,true,false)==OTA_BOOT_WAIT);
 CHECK(ota_boot_action(30000,7,true,true)==OTA_BOOT_ROLLBACK);
 puts("PASS: actual OTA engine, fragmented valid stream, product/chip/version, bounds, unauthorized/replay, incomplete/disconnect/cancel, deadlines, raw image and all I/O failures; bounded boot decision");
}
