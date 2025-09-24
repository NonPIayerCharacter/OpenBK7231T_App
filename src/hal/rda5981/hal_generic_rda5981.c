#if PLATFORM_RDA5981

#include "../hal_generic.h"
#include "wland_flash.h"
#include "rda_wdt_api.h"
#include "wait_api.h"

static wdt_t obj;

void HAL_RebootModule()
{
	//rda_wdt_softreset();
	rda_sys_softreset();
}

void HAL_Delay_us(int delay)
{
	wait_us(delay);
}

void HAL_Configure_WDT()
{
	rda_wdt_init(&obj, 10);
	rda_wdt_start(&obj);
}

void HAL_Run_WDT()
{
	rda_wdt_feed(&obj);
}

int HAL_FlashRead(char* buffer, int readlen, int startaddr)
{
	int res = rda5981_read_flash(startaddr, buffer, readlen);
	return !res;
}

int HAL_FlashWrite(char* buffer, int writelen, int startaddr)
{
	int res = rda5981_write_flash(startaddr, buffer, writelen);
	return !res;
}

int HAL_FlashEraseSector(int startaddr)
{
	int res = rda5981_erase_flash(startaddr, 0x1000);
	return !res;
}

#endif // PLATFORM_RDA5981
