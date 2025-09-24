#if PLATFORM_RDA5981

#include "rda_sys_wrapper.h"
#include "console.h"
#include "rda59xx_daemon.h"
#include "wland_flash.h"

extern "C" {
	extern void Main_Init();
	extern void Main_OnEverySecond();
}

__attribute__((used)) int main()
{
	Main_Init();
	while (true)
	{
		osDelay(1000);
		Main_OnEverySecond();
	}
}

#endif
