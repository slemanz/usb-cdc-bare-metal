#include "driver_systick.h"
#include "driver_clock.h"

static volatile uint64_t g_ticks = 0;

static void ticks_increment(void)
{
	g_ticks += 1;
}

void systick_init(uint32_t tick_hz)
{
    uint32_t count_value = clock_get()/tick_hz;

    // clear and load the reload value (24-bit counter)
    SYSTICK->LOAD = (count_value - 1) & 0x00FFFFFFU;

    // clear current value so first period is accurate
    SYSTICK->VAL = 0;

    // enable: processor clock source + interrupt + counter
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSRC | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;
}

uint64_t ticks_get(void)
{
    INTERRUPT_DISABLE();
    uint64_t snapshot = g_ticks;
    INTERRUPT_ENABLE();

    return snapshot;
}

void ticks_delay(uint64_t delay)
{
    uint64_t ticks_start = ticks_get();
    uint64_t wait = delay;

	if(wait < MAX_DELAY)
	{
		wait += 1;
	}

    while((ticks_get() - ticks_start) < wait)
    {
        __asm("NOP");
    }
}

void systick_counter(uint8_t EnorDi)
{
    if(EnorDi == ENABLE)
    {
        SYSTICK->CTRL = (SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_CLKSRC);
    }else
    {
        SYSTICK->CTRL &= ~(SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_CLKSRC);
    }
}

void systick_interrupt(uint8_t EnorDi)
{
    if(EnorDi == ENABLE)
    {
        SYSTICK->CTRL |= SYSTICK_CTRL_TICKINT;
    }else
    {
        SYSTICK->CTRL &= ~SYSTICK_CTRL_TICKINT;
    }
}

void systick_deinit(void)
{
    systick_counter(DISABLE);
    systick_interrupt(DISABLE);
}

void SysTick_Handler(void)
{
    ticks_increment();
}

void ticks_timeoutInit(ticks_timeout_t *pTimeout, uint32_t timeoutMs)
{
    pTimeout->start_tick = (uint32_t)ticks_get();
    pTimeout->timeoutMs = timeoutMs;
}

bool ticks_timeoutIsExpired(const ticks_timeout_t *pTimeout)
{
    uint32_t elapsed = ((uint32_t)ticks_get()) - pTimeout->start_tick;
    return (elapsed >= pTimeout->timeoutMs);
}