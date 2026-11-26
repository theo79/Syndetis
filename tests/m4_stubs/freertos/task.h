#pragma once
#include <stdint.h>
extern int64_t test_time_us;
typedef void *TaskHandle_t;
unsigned ulTaskNotifyTake(int clear, unsigned ticks);
void xTaskNotifyGive(TaskHandle_t task);
#ifndef TEST_RUNTIME_TASK
static inline int xTaskCreate(void (*f)(void*),const char*n,int s,void*a,int p,void*h){(void)f;(void)n;(void)s;(void)a;(void)p;(void)h;return 1;}
static inline void vTaskDelay(int n){test_time_us+=(int64_t)n*1000;}
#else
int xTaskCreate(void (*f)(void *), const char *name, int stack, void *arg, int priority, void *handle);
void vTaskDelay(int n);
#endif
