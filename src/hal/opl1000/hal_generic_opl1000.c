#if PLATFORM_OPL1000

#include <stdint.h>
#include <stdio.h>

#include "core_cm3.h"
#include "hal_system.h"
#include "hal_wdt.h"
#include "hal_flash.h"
#include "../hal_generic.h"

void HAL_RebootModule(void)
{
    if (Hal_Sys_SwResetAll != NULL)
    {
        Hal_Sys_SwResetAll();
    }

    NVIC_SystemReset();
}

void HAL_Delay_us(int delay)
{
    volatile int loops = delay * 8;
    while (loops-- > 0)
    {
        __asm volatile("nop");
    }
}

void HAL_Configure_WDT(void)
{
}

void HAL_Run_WDT(void)
{
    if (Hal_Wdt_Clear != NULL)
    {
        Hal_Wdt_Clear();
    }
}

int HAL_FlashRead(char* buffer, int readlen, int startaddr)
{
    Hal_Flash_AddrRead(0, startaddr, 0, readlen, (uint8_t*)buffer);
    return readlen;
}

int HAL_FlashWrite(char* buf, unsigned int len, unsigned int addr)
{
    Hal_Flash_AddrProgram(0, addr, 0, len, (uint8_t*)buf);
    return 0;
}

int HAL_FlashEraseSector(int startaddr)
{
    Hal_Flash_4KSectorAddrErase_impl(0, startaddr);
    return 0;
}

void HAL_RegisterPlatformSpecificCommands(void)
{
}

#endif
