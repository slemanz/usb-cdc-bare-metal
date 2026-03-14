#ifndef DRIVER_SYSTICK_H_
#define DRIVER_SYSTICK_H_

#include "stm32f411xx.h"
#include <stdbool.h>

#define TICK_HZ                 1000U
#define SYSTICK_TIM_CLK			HSI_CLOCK

#define SYSTICK_CTRL_ENABLE                 (1U << 0)
#define SYSTICK_CTRL_CLKSRC                 (1U << 2)
#define SYSTICK_CTRL_COUNTFLAG              (1U << 16)
#define SYSTICK_CTRL_TICKINT                (1U << 1)

typedef struct {
    uint32_t start_tick;
    uint32_t timeoutMs;
}ticks_timeout_t;

void systick_init(uint32_t tick_hz);
uint64_t ticks_get(void);
void ticks_delay(uint64_t delay);

void systick_counter(uint8_t EnorDi);
void systick_interrupt(uint8_t EnorDi);
void systick_deinit(void);

void ticks_timeoutInit(ticks_timeout_t *pTimeout, uint32_t timeoutMs);
bool ticks_timeoutIsExpired(const ticks_timeout_t *pTimeout);


#define MAX_DELAY   0xFFFFFFFF

#endif