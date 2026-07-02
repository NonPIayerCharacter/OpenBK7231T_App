#include "ameba.h"
#include "FreeRTOS.h"
#include "build_info.h"
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "ssl_rom_to_ram_map.h"
#include "rtk_compiler.h"
#include "ameba_cache.h"
#include <vfs.h>
#include <kv.h>
#include <../../../libraries/miniz/miniz.h>

#define MAGIC 						0xA5
#define ACK_MAGIC 					0x5A 

#define STATE_ERR					0xFF
#define STATE_SYN					0x00
#define STATE_RAM_DOWNLOAD			0x01
#define STATE_FLASH_DOWNLOAD		0x02
#define STATE_FLASH_UPLOAD			0x03
#define STATE_FLASH_ERASE			0x04
#define STATE_FLASH_CHIPERASE		0x05
#define STATE_RUN					0x06
#define STATE_BOUND					0x07
#define STATE_MAX					0x08

#define STATUS_SUCCESS				0x00
#define STATUS_ERROR				0x01
#define STATUS_ADDR_ERROR			0x02
#define STATUS_TYPE_ERROR			0x03
#define STATUS_LEN_ERROR			0x04
#define STATUS_CRC_ERROR			0x05

#define RESPONSE_FAIL				0xFF
#define RESPONSE_OK					0x00
#define RESPONSE_SYNC_BOOTROM		0x01
#define RESPONSE_SYNC_SBL			0x02

#define ACK_OK  0x00
#define ACK_ERR 0x01

#define MSG_OK 0x00
#define MSG_ERR -1

#define SYNC_REQUEST_VALUE			0x73796E63
#define SYNC_REQUEST_SIZE			0x04
#define SYNC_REQUEST_TIMEOUT		120

#define HEAD_SIZE					4
#define CFG_SIZE					8
#define ACK_SIZE					6
#define CMD_DATA_MAX_LEN 			(4 + 1 + 1024*64 + 2)
#define RESPONSE_SIZE				0x01
#define LOAD_MAX_SIZE_BIG			0x10000

struct sburner_cmd
{
	unsigned int msg_type;
	unsigned int arg0;
	unsigned int arg1;
};

struct message_rec_head
{
	unsigned char magic;
	unsigned char type;
	unsigned short data_len;
	unsigned int run_addr;
	unsigned char CRC8;
};

#define MESSAGE_REC_SIZE sizeof(struct message_rec_head)

struct message_ack_head
{
	unsigned char magic;
	unsigned char type;
	unsigned short data_len;
	unsigned char status;
	unsigned char CRC8;
};
#define MESSAGE_ACK_SIZE sizeof(struct message_ack_head)

struct load_cfg_msg
{
	unsigned int	addr;
	unsigned int	len;
} load_cfg_msg_t;
#define MESSAGE_LOAD_SIZE sizeof(struct load_cfg_msg)

#pragma pack(1)
typedef struct message_head
{
	uint8_t sof;
	uint8_t type;
	uint32_t data_len;
	uint8_t sub_type;
	uint32_t check_sum;
} message_head_t;
#pragma pack()

struct download_cfg_msg
{
	int msg_type;
	int addr;
	int len;
};

struct download_t
{
	int download_state;
	int download_addr;
	int download_len;
	int download_timeout;
	char* downloader_buf;
};

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC_MODE 'C'
#define XMODEM_BLOCK_SIZE_1K 1024
#define XMODEM_BLOCK_SIZE_128 128

uint32_t g_flash_id = 0;
unsigned int g_flash_size = 0;
struct message_ack_head ACK_msg =
{
	.magic = ACK_MAGIC,
	.type = 0,
	.data_len = 0,
	.status = STATUS_SUCCESS,
	.CRC8 = 0
};

unsigned char cmd_data_buf[CMD_DATA_MAX_LEN] = { 0 };

int uart_cmd_parser(void);

#ifdef CONFIG_AMEBADPLUS
#include "sysreg_ldo.h"
#define SPICCLKSL BIT_LSYS_CKSL_SPIC_XTAL
void BOOT_SOC_ClkSet(void)
{
	u32 Temp;
	u32 PeriDiV, SramDiv, QspiDiv;

	SocClk_Info_TypeDef pSocClk_Info_temp =
		//{
		//	600000000u,
		//	CORE_VOL_0P9,
		//	CLKDIV(2),
		//	CLKDIV(6),
		//	CLKDIV(4)
		//};
	{
		620000000u,
		CORE_VOL_0P9,
		CLKDIV(2),
		CLKDIV(6),
		CLKDIV(2)
	};
	SocClk_Info_TypeDef* pSocClk_Info = &pSocClk_Info_temp;
	u32 PllClk = pSocClk_Info->PLL_CLK;

	/*Do not Change Divider in FPGA*/
	if(SYSCFG_CHIPType_Get() == CHIP_TYPE_FPGA)
	{
		Temp = CPU_ClkGet();
		DelayClkUpdate(Temp);
		return;
	}

	// km4:Sram shall be 1:1 or 2:1, sram clk = km4 clk/SramDiv
	SramDiv = (PllClk / pSocClk_Info->KM4_CKD > SRAM_CLK_LIMIT) ? CLKDIV(2) : CLKDIV(1);
	PeriDiV = CLKDIV_ROUND_UP(PllClk, PERI_CLK_LIMIT);/*target clk PERI_CLK_LIMIT*/
	QspiDiv = CLKDIV_ROUND_UP(PllClk, QSPI_CLK_LIMIT);/*target clk QSPI_CLK_LIMIT*/

	/*0. configure core power according user setting */
	CORE_LDO_Vol_Set(pSocClk_Info->Vol_Type);

	//1. switch clk to XTAL
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKSL_GRP0);
	Temp = Temp & ~LSYS_MASK_CKSL_LP;	//KM0 XTAL
	Temp = Temp & ~LSYS_BIT_CKSL_HP;	//KM4 XTAL
	Temp = Temp & ~LSYS_BIT_CKSL_HPERI;	//hperi XTAL
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKSL_GRP0, Temp);
	//1.1 update CPU Clk used in DelayUs
	Temp = CPU_ClkGet();
	DelayClkUpdate(Temp);

	//2. modify PLL clock
	PLL_ClkSet(PllClk);

	//3. change ckd
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP0);
	Temp &= ~(LSYS_MASK_CKD_HP | LSYS_MASK_CKD_SRAM);
	Temp &= ~LSYS_MASK_CKD_LP;
	Temp &= ~LSYS_MASK_CKD_HPERI;
	Temp &= ~LSYS_MASK_CKD_QSPI;
	Temp |= LSYS_CKD_HP(pSocClk_Info->KM4_CKD - 1);
	Temp |= LSYS_CKD_LP(pSocClk_Info->KM0_CKD - 1);
	Temp |= LSYS_CKD_SRAM(SramDiv - 1);
	Temp |= LSYS_CKD_HPERI(PeriDiV - 1);
	Temp |= LSYS_CKD_QSPI(QspiDiv - 1);
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP0, Temp);

	//4. back to pll
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKSL_GRP0);
	Temp |= LSYS_BIT_CKSL_HP;
	Temp |= LSYS_BIT_CKSL_HPERI;
	Temp |= LSYS_CKSL_LP(CLK_KM0_PLL);
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKSL_GRP0, Temp);
	//4.1 update CPU Clk used in DelayUs
	Temp = CPU_ClkGet();
	DelayClkUpdate(Temp);

	PllClk = PLL_ClkGet();
	u8 flash_speed = Flash_Speed - 1;
	u8 spic_ckd = CLKDIV_ROUND_UP(PllClk, SPIC_CLK_LIMIT) - 1;
	flash_speed = MAX(flash_speed, spic_ckd);
	u32 Km4Clk = PllClk / pSocClk_Info->KM4_CKD;
	u32 Km0Clk = PllClk / pSocClk_Info->KM0_CKD;
	RTK_LOGI(NOTAG, "PLL CLK: %lu MHz\n", PllClk / 1000u / 1000);
	RTK_LOGI(NOTAG, "KM4 CPU CLK: %lu MHz\n", Km4Clk/ 1000u / 1000);
	RTK_LOGI(NOTAG, "KM0 CPU CLK: %lu MHz\n", Km0Clk/ 1000u / 1000);
	RTK_LOGI(NOTAG, "FLASH CLK: %lu MHz\n", PllClk / (2 * (flash_speed + 1)) / 1000u / 1000);
}
#elif CONFIG_AMEBALITE
#define SPICCLKSL BIT_LSYS_CKSL_SPIC_LBUS
u32 IPC_SEMTake(IPC_SEM_IDX SEM_Idx, u32 timeout)
{
	(void)SEM_Idx;
	(void)timeout;
	return true;
}
u32 IPC_SEMFree(IPC_SEM_IDX SEM_Idx)
{
	(void)SEM_Idx;
	return true;
}
void BOOT_SOC_ClkSet(void)
{
	u32 Temp;
	u32 PllMClk, PllDClk, PllClk;
	u32 HbusDiV, EcdsaDiv, CpuDiv, SramDiv;
	SocClk_Info_TypeDef* pSocClk_Info;
	RRAM_TypeDef* rram = RRAM_DEV;

	//u32 Boot_Clk_Config_Level = 0;
	//pSocClk_Info = &SocClk_Info[Boot_Clk_Config_Level];

	SocClk_Info_TypeDef pSocClk_Info_temp =
	{
		620000000u,
		500000000u,
		CORE_VOL_0P9,
		CLKDIV(2) | ISPLLM,
		CLKDIV(2) | ISPLLM
	};
	pSocClk_Info = &pSocClk_Info_temp;

	PllMClk = pSocClk_Info->PLLM_CLK;
	PllDClk = pSocClk_Info->PLLD_CLK;
	CpuDiv = pSocClk_Info->CPU_CKD & ~ISPLLD;

	if(pSocClk_Info->CPU_CKD & ISPLLD)
	{
		PllClk = PllDClk;
	}
	else
	{
		PllClk = PllMClk;
	}

	/* Note: for sram, max 240MHz under 1.0v, max 200MHz under 0.9v, and km4:Sram shall be 1:1 or 2:1 */
	if(pSocClk_Info->Vol_Type == CORE_VOL_0P9)
	{
		HbusDiV = CLKDIV_ROUND_UP(PllClk, HBUS_0P9V_CLK_LIMIT);
		if(PllClk / CpuDiv > SRAM_0P9V_CLK_LIMIT)
		{
			SramDiv = CpuDiv * 2;
		}
		else
		{
			SramDiv = CpuDiv;
		}
	}
	else
	{
		HbusDiV = CLKDIV_ROUND_UP(PllClk, HBUS_1P0V_CLK_LIMIT);
		if(PllClk / CpuDiv > SRAM_1P0V_CLK_LIMIT)
		{
			SramDiv = CpuDiv * 2;
		}
		else
		{
			SramDiv = CpuDiv;
		}
	}

	EcdsaDiv = CLKDIV_ROUND_UP(PllMClk, ECDSA_CLK_LIMIT);/*target clk ECDSA_CLK_LIMIT*/

	//0. configure core power according user setting
	SWR_CORE_Vol_Set(pSocClk_Info->Vol_Type);

	//1. select clk_lbus
	CPU_ClkSet_NonOS(CLK_CPU_LBUS);

	//2. gated selected PLL clock output (1-dsp_pll; 0-cpu_pll)
	PLL_TypeDef* PLL = (PLL_TypeDef*)PLL_BASE;
	PLL->PLL_CPUPLL_CTRL0 &= ~PLL_BIT_CPUPLL_CK_EN;
	PLL->PLL_PERIPLL_CTRL0 &= ~PLL_BIT_PERIPLL_CK_EN;

	//3. modify PLL clock
	PLL_ClkSet(CLK_CPU_MPLL, PllMClk);
	PLL_ClkSet(CLK_CPU_DPLL, PllDClk);
	rram->PLLD_Clk_Info = PllDClk;

	//4. Disable km4/kr4/sram divn
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKE_GRP1);
	Temp &= ~APBPeriph_PLL_CLOCK;
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKE_GRP1, Temp);

	//5. enable DSP/CPU PLL clock output (1-dsp_pll; 0-cpu_pll)
	PLL->PLL_CPUPLL_CTRL0 |= PLL_BIT_CPUPLL_CK_EN;
	PLL->PLL_PERIPLL_CTRL0 |= PLL_BIT_PERIPLL_CK_EN;

	//6. change ckd of km4/kr4/sram according to voltage
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP0);
	Temp &= ~(LSYS_MASK_CKD_KM4 | LSYS_MASK_CKD_KR4 | LSYS_MASK_CKD_SRAM);
	Temp &= ~(LSYS_MASK_CKD_HBUS | LSYS_MASK_CKD_GDMA_AXI);
	Temp &= ~LSYS_MASK_CKD_PSRAM;

	Temp |= LSYS_CKD_KM4(CpuDiv - 1) | LSYS_CKD_KR4(CpuDiv - 1) | LSYS_CKD_SRAM(SramDiv - 1);
	Temp |= LSYS_CKD_HBUS(HbusDiV - 1) | LSYS_CKD_GDMA_AXI(HbusDiV - 1);
	Temp |= LSYS_CKD_PSRAM(pSocClk_Info->PSRAMC_CKD - 1);
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP0, Temp);

	if(pSocClk_Info->PSRAMC_CKD & ISPLLD)
	{
		RCC_PeriphClockSource_PSRAM(BIT_LSYS_CKSL_PSRAM_DSPPLL);
	}
	else
	{
		RCC_PeriphClockSource_PSRAM(BIT_LSYS_CKSL_PSRAM_CPUPLL);
	}

	/* change ckd of ecdsa */
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP1);
	Temp = (Temp & ~LSYS_MASK_CKD_ECDSA) | LSYS_CKD_ECDSA(EcdsaDiv - 1);
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKD_GRP1, Temp);

	//7. enable km4/kr4/sram divn
	Temp = HAL_READ32(SYSTEM_CTRL_BASE, REG_LSYS_CKE_GRP1);
	Temp |= APBPeriph_PLL_CLOCK;
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_LSYS_CKE_GRP1, Temp);

	//8. select divn_clk
	if(pSocClk_Info->CPU_CKD & ISPLLD)
	{
		CPU_ClkSet_NonOS(CLK_CPU_DPLL);
	}
	else
	{
		CPU_ClkSet_NonOS(CLK_CPU_MPLL);
	}

	PllMClk = PLL_ClkGet(CLK_CPU_MPLL);
	PllDClk = PLL_ClkGet(CLK_CPU_DPLL);
	u8 flash_speed = FLASH_CLK_DIV10 - 8;
	u8 spic_ckd = CLKDIV_ROUND_UP(PllMClk, SPIC_CLK_LIMIT) - 1;
	flash_speed = MAX(flash_speed, spic_ckd);
	RTK_LOGI(NOTAG, "PLLM CLK: %lu MHz\n", PllMClk / 1000 / 1000);
	RTK_LOGI(NOTAG, "PLLD CLK: %lu MHz\n", PllDClk / 1000 / 1000);
	RTK_LOGI(NOTAG, "KM4 CPU CLK: %lu MHz\n", pSocClk_Info->CPU_CKD & ISPLLD ? PllDClk / CpuDiv / 1000 / 1000 : PllMClk / CpuDiv / 1000 / 1000);
	RTK_LOGI(NOTAG, "FLASH CLK: %lu MHz\n", PllMClk / (2 * (flash_speed + 1)) / 1000 / 1000);
}
#endif


//extern u32 crc32_get(u8* buf, int len);
//mz_ulong mz_crc32(mz_ulong crc, const mz_uint8* ptr, size_t buf_len)
//{
//	(void)crc;
//	return crc32_get((u8*)ptr, (int)buf_len);
//}

void flasher_stub(void)
{
	DCache_CleanInvalidate(0xFFFFFFFF, 0xFFFFFFFF);
	SCB_DisableDCache();
	_memset((void*)__image1_bss_start__, 0, (__image1_bss_end__ - __image1_bss_start__));

	void sburner_flash_init(void);
	sburner_flash_init();
	WDG_Refresh(IWDG_DEV);
	WDG_Timeout(IWDG_DEV, 0xFFFFFFFF);
	//WDG_ClearINT(IWDG_DEV, WDG_BIT_EIC);
	//InterruptRegister((IRQ_FUN)watchdog_irq_handler, IWDG_IRQ, 0, 3);
	//InterruptEn(IWDG_IRQ, 3);
	//WDG_INTConfig(IWDG_DEV, WDG_BIT_EIE, ENABLE);
	InterruptDis(UART_LOG_IRQ);
	BOOT_SOC_ClkSet();

	RCC_PeriphClockSource_SPIC(SPICCLKSL);
	FLASH_Read_HandShake_Cmd(0, DISABLE);
	FLASH_DeepPowerDown(DISABLE);
	if(SYSCFG_OTP_SPICAddr4ByteEn())
	{
		flash_init_para.FLASH_addr_phase_len = ADDR_4_BYTE;
	}
#ifdef CONFIG_AMEBADPLUS
	if (flash_init_para.FLASH_addr_phase_len == ADDR_4_BYTE) {
		FLASH_Addr4ByteEn();
	}
#endif
	flash_highspeed_setup();
	RCC_PeriphClockCmd(APBPeriph_SHA, APBPeriph_SHA_CLOCK, ENABLE);
	RCC_PeriphClockCmd(APBPeriph_LX, APBPeriph_LX_CLOCK, ENABLE);
	extern HeapRegion_t xHeapRegions[];
	bool os_heap_add(u8 * start_addr, size_t heap_size);
	os_heap_add((uint8_t*)0x20000000, (size_t)0xA000);
	os_heap_add((uint8_t*)0x20030000, (size_t)0x50000);
	vPortDefineHeapRegions(xHeapRegions);
	vfs_init();
	vfs_user_register(VFS_PREFIX, VFS_LITTLEFS, VFS_INF_FLASH, VFS_REGION_1, VFS_RW);
	rt_kv_init();
	CRYPTO_SHA_Init(NULL);
	LOGUART_SetBaud(LOGUART_DEV, 115200);
	while(1) uart_cmd_parser();
}

IMAGE1_ENTRY_SECTION
RAM_FUNCTION_START_TABLE RamStartTable = {
	.RamStartFun = flasher_stub,
	.RamWakeupFun = flasher_stub,
	.RamPatchFun0 = flasher_stub,
	.RamPatchFun1 = flasher_stub,
	.RamPatchFun2 = flasher_stub
};

//void rtk_log_write_nano(rtk_log_level_t level, const char* tag, const char letter, const char* fmt, ...) 
//{
//	(void)level;
//	(void)tag;
//	(void)letter;
//	(void)fmt;
//}

void FLASH_EraseXIP(u32 EraseType, u32 Address)
{
	FLASH_Erase(EraseType, Address);
}

int FLASH_ReadStream(u32 address, u32 len, u8* pbuf)
{
	_memcpy(pbuf, (const void*)(SPI_FLASH_BASE + address), len);

	return 1;
}

int FLASH_WriteStream(u32 address, u32 len, u8* pbuf)
{
	/* Check address: 4byte aligned & page(256bytes) aligned */
	u32 page_begin = address & (~0xff);
	u32 page_end = (address + len - 1) & (~0xff);
	u32 page_cnt = ((page_end - page_begin) >> 8) + 1;

	u32 addr_begin = address;
	u32 addr_end = (page_cnt == 1) ? (address + len) : (page_begin + 0x100);
	u32 size = addr_end - addr_begin;

	if(len == 0)
	{
		//RTK_LOGW(NOTAG, "function %s, data length is invalid (0) \r\n", __func__);
		goto exit;
	}

	if(IS_FLASH_ADDR((u32)pbuf))
	{
		//RTK_LOGE(NOTAG, "function %s, source address(%08x) can not be flash address\r\n", __func__, pbuf);
		//assert_param(0);
	}

	while(page_cnt)
	{
		FLASH_TxData(addr_begin, size, pbuf);
		pbuf += size;

		page_cnt--;
		addr_begin = addr_end;
		addr_end = (page_cnt == 1) ? (address + len) : (addr_begin + 0x100);
		size = addr_end - addr_begin;
	}

	//DCache_Invalidate(SPI_FLASH_BASE + address, len);
	//RSIP_MMU_Cache_Clean();

exit:
	return 1;
}

void uart_fifo_reset(void)
{
	LOGUART_ClearRxFifo(LOGUART_DEV);
}
void uart_putc(char ch)
{
#ifdef CONFIG_AMEBADPLUS
	extern LOG_UART_PORT LOG_UART_IDX_FLAG[];
	while(!LOGUART_Writable());
	LOGUART_WaitTx();
	LOGUART_DEV->LOGUART_UART_THRx[LOG_UART_IDX_FLAG[SYS_CPUID()].idx] = ch;
	LOGUART_WaitTxComplete();
#else
	LOGUART_WaitTxComplete();
	LOGUART_DEV->LOGUART_UART_THRx[0] = ch;
	LOGUART_WaitTxComplete();
#endif
}

void uart_write(unsigned char* data, uint32_t len)
{
	if(!data || !len)
	{
		return;
	}

	for(size_t i = 0; i < len; ++i)
	{
		uart_putc(data[i]);
	}
}

signed char uart_getc(uint8_t* out_byte, uint32_t timeout)
{
	uint64_t timeout_ticks = 0;
	
	if(out_byte == NULL)
		return -1;
	
	while(timeout_ticks++ < timeout * 10)
	{
		if(timeout_ticks % 100 == 0) WDG_Refresh(IWDG_DEV);
		u32 loguart_lsr = LOGUART_GetStatus(LOGUART_DEV);
		if((loguart_lsr & LOGUART_BIT_DRDY))
		//if(LOGUART_Readable())
		//if(LOGUART_GetRxCount())
		{
			*out_byte = (uint8_t)LOGUART_GetChar(false);
			return 0;
		}
		DelayUs(10);
	}

	return -1;    // timeout
}

void FLASH_EraseByLength(uint32_t Address, uint32_t Length)
{
	if((Address & 0x3) != 0)
		return;
	if(Length == 0)
		return;

	uint32_t cur = Address;
	uint32_t end = Address + Length;

	while(cur < end)
	{
		if((cur % 0x10000U == 0) && (end - cur >= 0x10000U))
		{
			FLASH_Erase(EraseBlock, cur);
			cur += 0x10000U;
		}
		else
		{
			FLASH_Erase(EraseSector, cur);
			cur += 0x1000U;
		}
	}

	return ;
}

unsigned char uboot_mesage_check(unsigned char* buf, unsigned short length)
{
	unsigned int crc = 0;
	unsigned char ret = 0;
	unsigned int i = 0;

	for(i = 0; i < length; i++)
	{
		crc += buf[i];
	}
	ret = crc % 256;
	return ret;
}

void sburner_flash_init(void)
{
	FLASH_RxCmd(flash_init_para.FLASH_cmd_rd_id, 3, (uint8_t*)&g_flash_id);

	unsigned char id = (g_flash_id >> 16) & 0xff;
	switch(id)
	{
		case 0x13:
		case 0x33:
			g_flash_size = 0x80000; break;
		case 0x14:
		case 0x34:
			g_flash_size = 0x100000; break;
		case 0x15:
		case 0x35:
			g_flash_size = 0x200000; break;
		default:
		case 0x16:
		case 0x36:
			g_flash_size = 0x400000; break;
		case 0x17:
		case 0x37:
			g_flash_size = 0x800000; break;
		case 0x18:
		case 0x38:
			g_flash_size = 0x1000000; break;
		case 0x19:
		case 0x39:
			g_flash_size = 0x2000000; break;
		case 0x1A:
		case 0x3A:
			g_flash_size = 0x4000000; break;
		case 0x1B:
			g_flash_size = 0x8000000; break;
		case 0x1C:
			g_flash_size = 0x10000000; break;
	}
}

static uint16_t crc16_ccitt(const uint8_t* data, uint16_t len)
{
	uint16_t crc = 0;
	while(len--)
	{
		crc ^= (uint16_t)(*data++) << 8;
		for(uint8_t i = 0; i < 8; i++)
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
	}
	return crc;
}

void uboot_sync(void)
{
	unsigned int i = 0;
	struct message_rec_head* msg = (struct message_rec_head*)cmd_data_buf;

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.status = STATUS_SUCCESS;

	//SYNC
	for(i = 0; i < SYNC_REQUEST_SIZE; i++)
	{
		if(cmd_data_buf[HEAD_SIZE + i] != (char)((SYNC_REQUEST_VALUE >> (i * 8)) & 0xff))
		{
			ACK_msg.status = STATUS_ERROR;
			ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
			uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
			return;	//eroor, again
		}
	}
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
}

void uboot_flash_erase_handle(void* buf)
{
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.status = STATUS_SUCCESS;

	_memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	FLASH_EraseByLength(cfg_msg.addr, cfg_msg.len);
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
}

void uboot_flash_chiperase_handle(void* buf)
{
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.status = STATUS_SUCCESS;

	FLASH_Erase(EraseChip, 0);
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
}

void uboot_buad(void)
{
	struct message_rec_head* msg = (struct message_rec_head*)cmd_data_buf;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
	LOGUART_SetBaud(LOGUART_DEV, msg->run_addr);
	DelayUs(100000);
	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
}

void uboot_flashid(void)
{
	struct message_rec_head* msg = (struct message_rec_head*)cmd_data_buf;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
	ACK_msg.data_len = 4;
	_memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);
	_memcpy(&cmd_data_buf[HEAD_SIZE], &g_flash_id, 4);
	cmd_data_buf[HEAD_SIZE + 4] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + 4 + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + 4 + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + 4 + 2);
}

void uboot_flash_xmodem_dl(void* buf)
{
	uint8_t header[3] = { 0x00 };
	uint8_t data[XMODEM_BLOCK_SIZE_1K] = { 0x00 };
	uint8_t crc_bytes[2] = { 0x00 };
	uint16_t crc_calc, crc_recv;
	uint32_t offset = 0;
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;
	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);

	_memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	FLASH_EraseByLength(cfg_msg.addr, cfg_msg.len);

	uart_putc(CRC_MODE);

	for(;;)
	{
		// header
		if(uart_getc(&header[0], 1000) != 0)
		{
			uart_putc(CRC_MODE);
			continue;
		}

		if(header[0] == EOT)
		{
			uart_putc(ACK);
			break;
		}

#define NAKCONTINUE { uart_putc(NAK); continue; }

		if(header[0] != STX) NAKCONTINUE

		if(uart_getc(&header[1], 2000) == -1) NAKCONTINUE
		if(uart_getc(&header[2], 2000) == -1) NAKCONTINUE

		if((header[1] + header[2]) != 0xFF) NAKCONTINUE

		// recv data + crc
		for(int i = 0; i < XMODEM_BLOCK_SIZE_1K; i++)
			if(uart_getc(&data[i], 2000) == -1) NAKCONTINUE
		if(uart_getc(&crc_bytes[0], 2000) == -1) NAKCONTINUE
		if(uart_getc(&crc_bytes[1], 2000) == -1) NAKCONTINUE

		crc_recv = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
		crc_calc = crc16_ccitt(data, XMODEM_BLOCK_SIZE_1K);

		if(crc_recv != crc_calc) NAKCONTINUE

		FLASH_WriteStream(cfg_msg.addr + offset, XMODEM_BLOCK_SIZE_1K, data);
		offset += XMODEM_BLOCK_SIZE_1K;

		uart_putc(ACK);
	}
	//LOGUART_SetBaud(LOGUART_DEV, 115200);
}

void uboot_flash_xmodem_ul(bool isRaw, void* buf)
{
	uint8_t block_num = 1;
	uint8_t resp = 0;
	uint32_t offset = 0;
	int retry;
	int ret;
	bool use_1k = true;
	bool use_crc = true;

	uint8_t packet[3 + XMODEM_BLOCK_SIZE_1K + 2];
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);

	_memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);
	uint32_t data_len = cfg_msg.len;

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	int timeout = 10000;
	while(timeout > 0)
	{
		if(uart_getc(&resp, 100) == 0)
		{
			if(resp == CRC_MODE)
			{
				use_crc = true;
				use_1k = true;
				break;
			}
			else if(resp == NAK)
			{
				use_crc = false;
				use_1k = false;
				break;
			}
			else if(resp == CAN)
			{
				return;
			}
		}

		timeout -= 100;
	}

	if(timeout <= 0)
	{
		uart_putc(CAN);
		uart_putc(CAN);
		return;
	}

	while(data_len > 0)
	{
		uint32_t block_size;
		uint8_t header;

		if(use_1k)
		{
			block_size = XMODEM_BLOCK_SIZE_1K;
			header = STX;
		}
		else
		{
			block_size = 128;
			header = SOH;
		}

		uint32_t chunk = (data_len >= block_size) ? block_size : data_len;

		retry = 0;
		while(retry < 10)
		{
			memset(packet, 0xFF, sizeof(packet));

			packet[0] = header;
			packet[1] = block_num;
			packet[2] = ~block_num;

			_memcpy(&packet[3], (const void*)((isRaw == false ? SPI_FLASH_BASE : 0) + cfg_msg.addr + offset), chunk);

			if(chunk < block_size)
			{
				memset(&packet[3 + chunk], 0xFF, block_size - chunk);
			}

			uint32_t pkt_len = 3 + block_size;
			if(use_crc)
			{
				uint16_t crc = crc16_ccitt(&packet[3], block_size);
				packet[pkt_len++] = (crc >> 8) & 0xff;
				packet[pkt_len++] = crc & 0xff;
			}
			else
			{
				uint8_t sum = 0;
				for(uint32_t i = 0; i < block_size; ++i)
				{
					sum += packet[3 + i];
				}
				packet[pkt_len++] = sum;
			}

			uart_write(packet, pkt_len);

			ret = uart_getc(&resp, 5000);
			if(ret == 0 && resp == ACK)
			{
				break;
			}

			if(ret == 0 && resp == CAN)
			{
				if(uart_getc(&resp, 1000) == 0 && resp == CAN)
				{
					return;
				}
			}

			++retry;
		}

		if(use_1k && retry >= 7)
		{
			use_1k = false;
		}

		if(retry >= 10)
		{
			uart_putc(CAN);
			uart_putc(CAN);
			LOGUART_SetBaud(LOGUART_DEV, 115200);
			return;
		}

		offset += chunk;
		data_len -= chunk;
		++block_num;
	}

	retry = 0;
	while(retry < 10)
	{
		uart_putc(EOT);

		ret = uart_getc(&resp, 5000);
		if(ret == 0 && resp == ACK)
		{
			return;
		}
		++retry;
	}

	uart_putc(CAN);
	uart_putc(CAN);
}

int mz_deflateInit3(mz_streamp pStream, int level, int method, int window_bits, int mem_level, int strategy)
{
	tdefl_compressor* pComp;
	mz_uint comp_flags = tdefl_create_comp_flags_from_zip_params(level, window_bits, strategy);

	if(!pStream)
		return MZ_STREAM_ERROR;
	if((method != MZ_DEFLATED) || ((mem_level < 1) || (mem_level > 9)) || ((window_bits != MZ_DEFAULT_WINDOW_BITS) && (-window_bits != MZ_DEFAULT_WINDOW_BITS)))
		return MZ_PARAM_ERROR;

	pStream->data_type = 0;
	pStream->adler = 0;
	pStream->msg = NULL;
	pStream->reserved = 0;
	pStream->total_in = 0;
	pStream->total_out = 0;
	if(!pStream->zalloc)
		pStream->zalloc = miniz_def_alloc_func;
	if(!pStream->zfree)
		pStream->zfree = miniz_def_free_func;

	pComp = (tdefl_compressor*)pStream->zalloc(pStream->opaque, 1, sizeof(tdefl_compressor));
	if(!pComp)
		return MZ_MEM_ERROR;

	pStream->state = (struct mz_internal_state*)pComp;

	if(tdefl_init(pComp, NULL, NULL, comp_flags) != TDEFL_STATUS_OKAY)
	{
		mz_deflateEnd(pStream);
		return MZ_PARAM_ERROR;
	}

	return MZ_OK;
}

void uboot_flash_xmodem_ul_z(void* buf)
{
	uint8_t block_num = 1;
	uint8_t resp = 0;
	int retry;
	int ret;
	bool use_1k = true;
	bool use_crc = true;

	uint8_t packet[3 + XMODEM_BLOCK_SIZE_1K + 2];

	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);

	uint8_t comp_level = cmd_data_buf[HEAD_SIZE + CFG_SIZE];
	if(comp_level < 1 || comp_level > 10) comp_level = 5;

	_memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	int timeout = 10000;

	while(timeout > 0)
	{
		if(uart_getc(&resp, 1000) == 0)
		{
			if(resp == CRC_MODE)
			{
				use_crc = true;
				use_1k = true;
				break;
			}

			if(resp == NAK)
			{
				use_crc = false;
				use_1k = false;
				break;
			}

			if(resp == CAN) return;
		}

		timeout -= 1000;
	}

	if(timeout <= 0)
	{
		uart_putc(CAN);
		uart_putc(CAN);
		return;
	}

	z_stream stream;

	memset(&stream, 0, sizeof(stream));

	const uint8_t* src = (const uint8_t*)(SPI_FLASH_BASE + cfg_msg.addr);

	uint32_t remaining = cfg_msg.len;

	if(mz_deflateInit3(&stream, comp_level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != Z_OK)
	{
		return;
	}

	bool finished = false;

	while(!finished)
	{
		uint32_t block_size;
		uint8_t header;

		if(use_1k)
		{
			block_size = XMODEM_BLOCK_SIZE_1K;
			header = STX;
		}
		else
		{
			block_size = 128;
			header = SOH;
		}

		memset(packet, 0xFF, sizeof(packet));

		packet[0] = header;
		packet[1] = block_num;
		packet[2] = ~block_num;

		stream.next_out = &packet[3];
		stream.avail_out = block_size;

		while(stream.avail_out)
		{
			if(stream.avail_in == 0 && remaining)
			{
				uint32_t n = remaining;

				if(n > block_size) n = block_size;

				stream.next_in = (unsigned char*)(src + (cfg_msg.len - remaining));

				stream.avail_in = n;

				remaining -= n;
			}

			WDG_Refresh(IWDG_DEV);

			ret = deflate(&stream, remaining ? Z_NO_FLUSH : Z_FINISH);

			WDG_Refresh(IWDG_DEV);

			if(ret == Z_STREAM_END)
			{
				finished = true;
				break;
			}

			if(ret != Z_OK)
			{
				deflateEnd(&stream);
				return;
			}
		}

		uint32_t pkt_len = 3 + block_size;

		if(use_crc)
		{
			uint16_t crc = crc16_ccitt(&packet[3], block_size);

			packet[pkt_len++] = crc >> 8;
			packet[pkt_len++] = crc & 0xff;
		}
		else
		{
			uint8_t sum = 0;

			for(uint32_t i = 0; i < block_size; i++)
			{
				sum += packet[3 + i];
			}

			packet[pkt_len++] = sum;
		}

		retry = 0;

		while(retry < 10)
		{
			uart_write(packet, pkt_len);

			ret = uart_getc(&resp, 5000);

			if(ret == 0 && resp == ACK)
			{
				break;
			}

			retry++;
		}

		//if(use_1k && retry >= 7)
		//{
		//	use_1k = false;
		//}

		if(retry >= 10)
		{
			deflateEnd(&stream);

			uart_putc(CAN);
			uart_putc(CAN);

			return;
		}

		block_num++;
	}

	deflateEnd(&stream);

	retry = 0;

	while(retry < 10)
	{
		uart_putc(EOT);

		ret = uart_getc(&resp, 5000);

		if(ret == 0 && resp == ACK)
		{
			return;
		}

		retry++;
	}

	uart_putc(CAN);
	uart_putc(CAN);
}

void uboot_flash_sha256(void* buf)
{
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 32;

	_memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.data_len = 0;
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}
	ALIGNMTO(CACHE_LINE_SIZE) u8 hash[32] = { 0 };
	hw_sha_context  ctx = { 0 };
	rtl_crypto_sha2_init(SHA2_256 , &ctx);
	uint32_t addr = cfg_msg.addr;
	uint32_t remaining = cfg_msg.len;
	while(remaining > 0)
	{
		uint32_t chunk = remaining > 0x40000 ? 0x40000 : remaining;
		WDG_Refresh(IWDG_DEV);
		rtl_crypto_sha2_update((uint8_t*)(SPI_FLASH_BASE + addr), chunk, &ctx);
		addr += chunk;
		remaining -= chunk;
	}
	
	rtl_crypto_sha2_final(hash, &ctx);

	_memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);
	_memcpy(&cmd_data_buf[HEAD_SIZE], &hash, 32);
	cmd_data_buf[HEAD_SIZE + 32] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + 32 + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + 32 + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + 32 + 2);
}

void uboot_kv_get(void* buf)
{
	struct message_rec_head* msg = (struct message_rec_head*)buf;
	char kvname[MAX_KEY_LENGTH] = { 0 };
	strncpy((char*)&kvname, (const char*)&(cmd_data_buf[HEAD_SIZE]), msg->data_len);
	int kvsize = rt_kv_size(kvname);

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = kvsize;

	if(kvsize <= 0)
	{
		ACK_msg.data_len = 0;
		ACK_msg.status = STATUS_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	_memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);
	rt_kv_get(kvname, &cmd_data_buf[HEAD_SIZE], kvsize);
	cmd_data_buf[HEAD_SIZE + kvsize] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + kvsize + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + kvsize + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + kvsize + 2);
}

void uboot_kv_set(void* buf)
{
	uint8_t header[3] = { 0x00 };
	uint8_t data[XMODEM_BLOCK_SIZE_1K] = { 0x00 };
	uint8_t crc_bytes[2] = { 0x00 };
	uint16_t crc_calc, crc_recv;
	uint32_t offset = 0;
	struct message_rec_head* msg = (struct message_rec_head*)buf;
	char kvname[16] = { 0 };
	unsigned char namelen = cmd_data_buf[HEAD_SIZE];
	strncpy((char*)&kvname, (const char*)&(cmd_data_buf[HEAD_SIZE + 3]), namelen);
	uint16_t datasize = cmd_data_buf[HEAD_SIZE + 1] | cmd_data_buf[HEAD_SIZE + 2] << 8;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0;
	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	for(;;)
	{
		// header
		if(uart_getc(&header[0], 1000) != 0)
		{
			uart_putc(CRC_MODE);
			continue;
		}

		if(header[0] == EOT)
		{
			uart_putc(ACK);
			break;
		}

#define NAKCONTINUE { uart_putc(NAK); continue; }

		if(header[0] != STX) NAKCONTINUE

		if(uart_getc(&header[1], 2000) == -1) NAKCONTINUE
		if(uart_getc(&header[2], 2000) == -1) NAKCONTINUE

		if((header[1] + header[2]) != 0xFF) NAKCONTINUE

		// recv data + crc
		for(int i = 0; i < XMODEM_BLOCK_SIZE_1K; i++)
			if(uart_getc(&data[i], 2000) == -1) NAKCONTINUE
		if(uart_getc(&crc_bytes[0], 2000) == -1) NAKCONTINUE
		if(uart_getc(&crc_bytes[1], 2000) == -1) NAKCONTINUE

		crc_recv = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
		crc_calc = crc16_ccitt(data, XMODEM_BLOCK_SIZE_1K);

		if(crc_recv != crc_calc) NAKCONTINUE

		_memcpy(cmd_data_buf + offset, data, XMODEM_BLOCK_SIZE_1K);
		offset += XMODEM_BLOCK_SIZE_1K;

		uart_putc(ACK);
	}
	rt_kv_set(kvname, &cmd_data_buf, datasize);
}

void uboot_flash_xmodem_dl_z(void* buf)
{
	uint8_t header[3] = { 0x00 };
	uint8_t data[XMODEM_BLOCK_SIZE_1K] = { 0xFF };
	uint8_t crc_bytes[2] = { 0x00 };
	uint16_t crc_calc, crc_recv;
	uint32_t flash_offset = 0;
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.status = STATUS_SUCCESS;
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 0x0000;
	ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	FLASH_EraseByLength(cfg_msg.addr, cfg_msg.len);

	mz_stream stream;
	memset(&stream, 0, sizeof(stream));
	if(mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
	{
		ACK_msg.status = STATUS_ERROR;
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	uart_putc(CRC_MODE);

	flash_offset = cfg_msg.addr;

	for(;;)
	{
		uint32_t data_size = 0;

		if(uart_getc(&header[0], 3333) != 0)
		{
			uart_putc(CRC_MODE);
			continue;
		}

		if(header[0] == EOT)
		{
			stream.next_in = NULL;
			stream.avail_in = 0;

			while(1)
			{
				stream.next_out = cmd_data_buf;
				stream.avail_out = sizeof(cmd_data_buf);

				int status = mz_inflate(&stream, MZ_NO_FLUSH);
				uint32_t produced = sizeof(cmd_data_buf) - stream.avail_out;

				if(produced > 0)
				{
					if((flash_offset + produced) > (cfg_msg.addr + cfg_msg.len))
					{
						goto abort_decompression;
					}

					FLASH_WriteStream(flash_offset, produced, cmd_data_buf);
					flash_offset += produced;
				}

				if(status == MZ_STREAM_END) break;

				if(status != MZ_OK && status != MZ_BUF_ERROR)
					goto abort_decompression;
			}

			mz_inflateEnd(&stream);

			uart_putc(ACK);
			return;
		}

		if(header[0] != STX && header[0] != SOH)
		{
			uart_putc(NAK);
			continue;
		}

		data_size = (header[0] == STX) ? XMODEM_BLOCK_SIZE_1K : XMODEM_BLOCK_SIZE_128;

		uart_getc(&header[1], 10000);
		uart_getc(&header[2], 10000);

		if((header[1] + header[2]) != 0xFF)
		{
			uart_putc(NAK);
			continue;
		}

		for(uint32_t i = 0; i < data_size; i++)
			uart_getc(&data[i], 20000);

		uart_getc(&crc_bytes[0], 10000);
		uart_getc(&crc_bytes[1], 10000);

		crc_recv = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
		crc_calc = crc16_ccitt(data, data_size);

		if(crc_recv != crc_calc)
		{
			uart_putc(NAK);
			continue;
		}

		stream.next_in = data;
		stream.avail_in = data_size;

		while(stream.avail_in > 0)
		{
			stream.next_out = cmd_data_buf;
			stream.avail_out = sizeof(cmd_data_buf);

			int status = mz_inflate(&stream, MZ_NO_FLUSH);
			uint32_t produced = sizeof(cmd_data_buf) - stream.avail_out;

			if(produced > 0)
			{
				if((flash_offset + produced) > (cfg_msg.addr + cfg_msg.len))
				{
					goto abort_decompression;
				}

				FLASH_WriteStream(flash_offset, produced, cmd_data_buf);
				flash_offset += produced;
			}

			if(status == MZ_STREAM_END) break;

			if(status != MZ_OK && status != MZ_BUF_ERROR)
				goto abort_decompression;
		}

		uart_putc(ACK);
	}

abort_decompression:
	mz_inflateEnd(&stream);

	uart_putc(CAN);
	uart_putc(CAN);
}

void uboot_read_efuse(void)
{
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = 0x99;
	ACK_msg.data_len = OTP_LMAP_LEN;

	memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);

	OTP_LogicalMap_Read(&cmd_data_buf[HEAD_SIZE], 0, OTP_LMAP_LEN);

	cmd_data_buf[HEAD_SIZE + ACK_msg.data_len] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + ACK_msg.data_len + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + ACK_msg.data_len + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + ACK_msg.data_len + 2);
}

void uboot_read_otp(void)
{
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = 0x99;
	ACK_msg.data_len = 0x400;

	memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);

	FLASH_RxData(0x48, 0, 0x400, cmd_data_buf + HEAD_SIZE);

	cmd_data_buf[HEAD_SIZE + 0x400] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + 0x400 + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + 0x400 + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + 0x400 + 2);
}

int uart_cmd_parser(void)
{
	unsigned int i = 0;
	signed char resp = 0;
	unsigned char CRC8 = 0;
	unsigned char buf[HEAD_SIZE] = { 0 };
	struct message_rec_head rec_head;

	unsigned char cfgbuf[CFG_SIZE] = { 0 };

	uart_fifo_reset();

	do
	{
		memset(buf, 0, HEAD_SIZE);
		for(i = 0; i < HEAD_SIZE; ++i)
		{
			resp = uart_getc((unsigned char*)&buf[i], 0xFFFFFFFF);
			if(resp < 0)
			{
				resp = MSG_ERR;
				return resp;
			}
			if((buf[0]) != MAGIC)
			{
				i = 0xFFFFFFFF;
			}
		}

		_memcpy(cmd_data_buf, buf, HEAD_SIZE);
		_memcpy(&rec_head, buf, HEAD_SIZE);

		if(buf[1] <= 0x9F)
		{
			for(i = 0; i < rec_head.data_len; i++)
			{
				resp = uart_getc((unsigned char*)&cfgbuf[i], 0xFFFFFFFF);
				if(resp < 0)
				{
					resp = MSG_ERR;
					return resp;
				}
			}
			_memcpy(&(cmd_data_buf[HEAD_SIZE]), cfgbuf, rec_head.data_len);

			resp = uart_getc((unsigned char*)&cmd_data_buf[HEAD_SIZE + rec_head.data_len], 0xFFFFFFFF);
			if(resp < 0)
			{
				resp = MSG_ERR;
				return resp;
			}
			CRC8 = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + rec_head.data_len);
			if(cmd_data_buf[HEAD_SIZE + rec_head.data_len] != CRC8)
			{
				rec_head.type = STATE_ERR;
			}
		}
		else
		{
			rec_head.type = STATE_ERR;
		}


		switch(rec_head.type)
		{
			case STATE_SYN:
				uboot_sync();
				break;
			case STATE_FLASH_ERASE: // 0x04
				uboot_flash_erase_handle(&cmd_data_buf);
				break;
			case STATE_FLASH_CHIPERASE: // 0x05
				uboot_flash_chiperase_handle(&cmd_data_buf);
				break;
			case STATE_BOUND: // 0x07
				uboot_buad();
				break;
			case 0x09:
				uboot_flash_sha256(&cmd_data_buf);
				break;
			case 0x90:
				uboot_flashid();
				break;
			case 0x91:
				uboot_flash_xmodem_dl(&cmd_data_buf);
				break;
			case 0x92:
				uboot_flash_xmodem_ul(false, &cmd_data_buf);
				break;
			case 0x93:
				uboot_kv_get(&cmd_data_buf);
				break;
			case 0x94:
				uboot_kv_set(&cmd_data_buf);
				break;
			case 0x96:
				uboot_flash_xmodem_ul_z(&cmd_data_buf);
				break;
			case 0x97:
				uboot_flash_xmodem_dl_z(&cmd_data_buf);
				break;
			case 0x98:
				uboot_flash_xmodem_ul(true, &cmd_data_buf);
				break;
			case 0x99:
				uboot_read_efuse();
				break;

			default:
				ACK_msg.type = STATE_ERR;
				ACK_msg.status = STATUS_TYPE_ERROR;
				ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
				uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
				break;
		}
	} while(1);
}
