#include "../hal_hwtimer.h"

#if PLATFORM_MT7687

#include "hal_gpt.h"

static bool isTimerInit = false;
static float period = 0;

int8_t HAL_RequestHWTimer(float requestPeriodUs, float* realPeriodUs, HWTimerCB callback, void* arg)
{
	if(isTimerInit) return -1;
	if(realPeriodUs) *realPeriodUs = period = ((float)((uint32_t)(requestPeriodUs * 32768.0f / 1000000.0f)) * 1000000U / 32768U) + ((1.0f / 32768.0f) * 1000000U);
	isTimerInit = true;
	hal_gpt_init(1);
	hal_gpt_register_callback(1, callback, arg);
	return 0;
}

void HAL_HWTimerStart(int8_t timer)
{
	if(timer == -1) return;
	hal_gpt_start_timer_us(1, (uint32_t)period, HAL_GPT_TIMER_TYPE_REPEAT);
}

void HAL_HWTimerStop(int8_t timer)
{
	if(timer == -1) return;
	hal_gpt_stop_timer(1);
}

void HAL_HWTimerDeinit(int8_t timer)
{
	if(timer == -1) return;
	hal_gpt_deinit(1);
	isTimerInit = false;
}

#endif
