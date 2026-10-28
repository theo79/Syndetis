#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#define ESP_ERR_NO_MEM 0x101
#include "../main/discovery.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static jmp_buf idle;
static bool allocation_fails,task_fails;
QueueHandle_t xQueueCreate(unsigned n,size_t size){CHECK(n==4&&size==sizeof(queued_frame_t));return allocation_fails?NULL:(void*)1;}
void vQueueDelete(QueueHandle_t q){CHECK(q);}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned s,void*a,unsigned p,void*h){(void)a;(void)p;(void)h;CHECK(fn==parse_worker&&s==8192);(void)n;return task_fails?0:pdPASS;}
int xQueueReceive(QueueHandle_t q,void*p,unsigned t){(void)q;(void)p;(void)t;longjmp(idle,1);}
int xQueueSend(QueueHandle_t q,const void*p,unsigned t){(void)q;(void)p;(void)t;return pdTRUE;}
int64_t esp_timer_get_time(void){return 1000000;}
int main(void){
 CHECK(!discovery_worker_started());allocation_fails=true;CHECK(discovery_init()==ESP_ERR_NO_MEM&&!discovery_worker_started());
 allocation_fails=false;task_fails=true;CHECK(discovery_init()==ESP_ERR_NO_MEM&&!discovery_worker_started());
 task_fails=false;CHECK(discovery_init()==ESP_OK);CHECK(discovery_summary().available&&!discovery_worker_started());
 if(!setjmp(idle))parse_worker(NULL);
 CHECK(discovery_worker_started());CHECK(!table.link_up); // No network traffic/cable required.
 puts("PASS: actual discovery task startup handshake; allocation/task failures and no-cable/no-advertisement boot readiness");
}
