#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "platform_def.h"
#include "rom_flash.h"
#include "init_rom.h"
#include "rom_export_mbedtls.h"
#include "mbedtls/entropy_poll.h"
#include "../../../libraries/miniz/miniz.h"

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
//#define CMD_DATA_MAX_LEN 			(4 + 1 + 1024*64 + 2)
#define CMD_DATA_MAX_LEN 			(1024*4)
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

uint32_t stub_uart = USART0;

#define TICK_RATE_HZ ((uint32_t)1000)

#if defined(__GNUC__)
int _write(int fd, char *str, int len)
{
	(void)fd;
	int32_t i = 0;

	/* Send string and return the number of characters written */
	while (i != len) {
		while(RESET == usart_flag_get(stub_uart, USART_FLAG_TBE));
		usart_data_transmit(stub_uart, *str);
		str++;
		i++;
	}

	while(RESET == usart_flag_get(stub_uart, USART_FLAG_TC));

	return i;
}
#endif

static uint32_t fac_us = 0;

static uint8_t alloc_buf[0x3000];
struct rom_api_t* p_rom_api = (struct rom_api_t*)ROM_API_ARRAY_BASE;
uint32_t clock_us_factor;

uint32_t g_flash_id = 0x00160000; // fake flash id
unsigned int g_flash_size = 0x4000000;

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

void systick_init(void)
{
	clock_us_factor = SystemCoreClock / 1000000;
}

void sys_udelay(uint32_t nus)
{
	uint64_t start_mtime, delta_mtime;
	uint64_t tmp = SysTimer_GetLoadValue();

	do
	{
		start_mtime = SysTimer_GetLoadValue();
	} while(start_mtime == tmp);

	tmp = clock_us_factor * nus;

	do
	{
		delta_mtime = SysTimer_GetLoadValue() - start_mtime;
	} while(delta_mtime < tmp);
}

void uart_write(uint8_t* buf, uint32_t len)
{
	_write(-1, buf, len);
}

void uart_fifo_reset(unsigned int uart_base)
{
	usart_command_enable(stub_uart, USART_CMD_RXFCMD | USART_CMD_TXFCMD);
}

int8_t uart_getc(uint32_t uart, char* ch, uint32_t timeout_ms)
{
	uint64_t start;
	uint64_t timeout_ticks;

	timeout_ticks = (uint64_t)clock_us_factor * timeout_ms * 1000;
	if(timeout_ms = 0xFFFFFFFF) timeout_ticks = 0xFFFFFFFFFFFFFFFF;
	start = SysTimer_GetLoadValue();

	while((SysTimer_GetLoadValue() - start) < timeout_ticks)
	{
		if(RESET != usart_flag_get(uart, USART_FLAG_ORERR))
		{
			usart_flag_clear(uart, USART_FLAG_ORERR);
		}

		if(RESET != usart_flag_get(uart, USART_FLAG_RBNE))
		{
			*ch = (char)usart_data_receive(uart);
			return 0;
		}
	}

	return -1;
}

void uart_put_char(unsigned int uart, const char buf)
{
	usart_data_transmit(uart, (uint8_t)buf);
	while(RESET == usart_flag_get(uart, USART_FLAG_TBE));
}

static void uart_init(uint32_t usart_periph)
{
	if (usart_periph == UART2) {
		rcu_periph_clock_enable(RCU_UART2);
		rcu_periph_clock_enable(RCU_GPIOA);
		gpio_af_set(GPIOA, GPIO_AF_10, GPIO_PIN_6);  // UART2 TX
		gpio_af_set(GPIOA, GPIO_AF_8, GPIO_PIN_7);   // UART2 RX
		gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_6);
		gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_6);
		gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_7);
		gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_7);
	} else if (usart_periph == UART1) {
		rcu_periph_clock_enable(RCU_GPIOA);
		rcu_periph_clock_enable(RCU_UART1);
		gpio_af_set(GPIOA, GPIO_AF_0, GPIO_PIN_4);   // UART1 TX
		gpio_af_set(GPIOA, GPIO_AF_0, GPIO_PIN_5);   // UART1 RX
		gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_4);
		gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_4);
		gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_5);
		gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_5);
	} else if (usart_periph == USART0) {
		rcu_periph_clock_enable(RCU_USART0);
		rcu_periph_clock_enable(RCU_GPIOA);
		rcu_periph_clock_enable(RCU_GPIOB);
		gpio_af_set(GPIOA, GPIO_AF_2, GPIO_PIN_8);   // UART0 TX
		gpio_af_set(GPIOB, GPIO_AF_8, GPIO_PIN_15);  // UART0 RX
		gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_8);
		gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_8);
		gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_15);
		gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_15);
	} else {
		return;
	}

	usart_deinit(usart_periph);
	usart_baudrate_set(usart_periph, 115200U);
	usart_receive_config(usart_periph, USART_RECEIVE_ENABLE);
	usart_transmit_config(usart_periph, USART_TRANSMIT_ENABLE);
	//usart_interrupt_enable(usart_periph, USART_INT_RBNE);
	//usart_parity_config(usart_periph, USART_PM_EVEN);
	usart_receive_fifo_enable(usart_periph);

	usart_enable(usart_periph);
}

void stub_uart_idle_wait(void)
{
	while (RESET == usart_flag_get(stub_uart, USART_FLAG_TC));
}

//tlsf_t memalloc;
__attribute__((__used__)) void* __wrap_malloc(size_t size)
{
	//return tlsf_malloc(memalloc, size);
	// since it's the only allocation, just return raw address
	return (void*)(0x20002000 + 96 * 1024);
}
//void* __wrap_realloc(void* ptr, size_t size)
//{
//	return tlsf_realloc(memalloc, ptr, size);
//}
//void __wrap_free(void* ptr)
//{
//	tlsf_free(memalloc, ptr);
//}

int main(void)
{
	rom_symbol_init();
	mbedtls_memory_buffer_alloc_init(alloc_buf, sizeof(alloc_buf));
	//memalloc = tlsf_create_with_pool((void*)(0x20002000 + 96 * 1024), 0x26000 + 0x8000);
	systick_init();
	rcu_periph_clock_enable(RCU_HAU);
	rcu_periph_clock_enable(RCU_CRC);
	rcu_periph_clock_enable(RCU_DMA);
	rom_digest_haudma_en(1);

	mbedtls_platform_set_hardware_poll(rom_hardware_poll);
	mbedtls_ecp_curve_val_init();
	uart_init(USART0);
	uart_init(UART1);
	uart_init(UART2);

	while(1)
	{
		char ch = 0;
		if(uart_getc(USART0, &ch, 100) == 0)
		{
			stub_uart = USART0;
			break;
		}
		if(uart_getc(UART1, &ch, 100) == 0)
		{
			stub_uart = UART1;
			break;
		}
		if(uart_getc(UART2, &ch, 100) == 0)
		{
			stub_uart = UART2;
			break;
		}
	}
	while(1) uart_cmd_parser();
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

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	flash_erase(cfg_msg.addr, cfg_msg.len);
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

	flash_erase_chip();
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
	usart_disable(stub_uart);
	usart_baudrate_set(stub_uart, msg->run_addr);
	usart_enable(stub_uart);
	sys_udelay(50000);
	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
}

void uboot_flash_xmodem_dl(void* buf)
{
	uint8_t header[3] = { 0x00 };
	uint8_t data[XMODEM_BLOCK_SIZE_1K] = { 0xFF };
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

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
	sys_udelay(100000);

	flash_erase(cfg_msg.addr, cfg_msg.len);

	uart_put_char(stub_uart, CRC_MODE);

	for(;;)
	{
		// header
		if(uart_getc(stub_uart, &header[0], 3333) != 0)
		{
			uart_put_char(stub_uart, CRC_MODE);
			continue;
		}

		if(header[0] == EOT)
		{
			uart_put_char(stub_uart, ACK);
			break;
		}

		if(header[0] != STX)
		{
			uart_put_char(stub_uart, NAK);
			continue;
		}

		uart_getc(stub_uart, &header[1], 10000);
		uart_getc(stub_uart, &header[2], 10000);

		if((header[1] + header[2]) != 0xFF)
		{
			uart_put_char(stub_uart, NAK);
			continue;
		}

		// recv data + crc
		for(int i = 0; i < XMODEM_BLOCK_SIZE_1K; i++)
			uart_getc(stub_uart, &data[i], 20000);
		uart_getc(stub_uart, &crc_bytes[0], 10000);
		uart_getc(stub_uart, &crc_bytes[1], 10000);

		crc_recv = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
		crc_calc = crc16_ccitt(data, XMODEM_BLOCK_SIZE_1K);

		if(crc_recv != crc_calc)
		{
			uart_put_char(stub_uart, NAK);
			continue;
		}

		flash_write(cfg_msg.addr + offset, data, XMODEM_BLOCK_SIZE_1K);
		offset += XMODEM_BLOCK_SIZE_1K;

		uart_put_char(stub_uart, ACK);
	}
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

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);
	uint32_t data_len = cfg_msg.len;

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	int timeout = 10000;
	while(timeout > 0)
	{
		if(uart_getc(stub_uart, &resp, 100) == 0)
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
		uart_put_char(stub_uart, CAN);
		uart_put_char(stub_uart, CAN);
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

			//flash_read(cfg_msg.addr + offset, &packet[3], chunk);
			memcpy(&packet[3], (void*)((isRaw == false ? 0x8000000 : 0) + cfg_msg.addr + offset), chunk);

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

			ret = uart_getc(stub_uart, &resp, 5000);
			if(ret == 0 && resp == ACK)
			{
				break;
			}

			if(ret == 0 && resp == CAN)
			{
				if(uart_getc(stub_uart, &resp, 1000) == 0 && resp == CAN)
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
			uart_put_char(stub_uart, CAN);
			uart_put_char(stub_uart, CAN);
			return;
		}

		offset += chunk;
		data_len -= chunk;
		++block_num;
	}

	retry = 0;
	while(retry < 10)
	{
		uart_put_char(stub_uart, EOT);

		ret = uart_getc(stub_uart, &resp, 5000);
		if(ret == 0 && resp == ACK)
		{
			return;
		}
		++retry;
	}

	uart_put_char(stub_uart, CAN);
	uart_put_char(stub_uart, CAN);
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

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	uart_write((unsigned char*)&ACK_msg, ACK_SIZE);

	int timeout = 10000;

	while(timeout > 0)
	{
		if(uart_getc(stub_uart, &resp, 1000) == 0)
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
		uart_put_char(stub_uart, CAN);
		uart_put_char(stub_uart, CAN);
		return;
	}

	z_stream stream;

	memset(&stream, 0, sizeof(stream));

	const uint8_t* src = (const uint8_t*)(FLASH_BASE + cfg_msg.addr);

	uint32_t remaining = cfg_msg.len;
	int err = mz_deflateInit3(&stream, comp_level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
	if(err != Z_OK)
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

			ret = deflate(&stream, remaining ? Z_NO_FLUSH : Z_FINISH);

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

			ret = uart_getc(stub_uart, &resp, 5000);

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

			uart_put_char(stub_uart, CAN);
			uart_put_char(stub_uart, CAN);

			return;
		}

		block_num++;
	}

	deflateEnd(&stream);

	retry = 0;

	while(retry < 10)
	{
		uart_put_char(stub_uart, EOT);

		ret = uart_getc(stub_uart, &resp, 5000);

		if(ret == 0 && resp == ACK)
		{
			return;
		}

		retry++;
	}

	uart_put_char(stub_uart, CAN);
	uart_put_char(stub_uart, CAN);
}

void uboot_flash_sha256(void* buf)
{
	struct load_cfg_msg cfg_msg;
	struct message_rec_head* msg = (struct message_rec_head*)buf;

	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = msg->type;
	ACK_msg.data_len = 32;

	memcpy(&cfg_msg, &(cmd_data_buf[HEAD_SIZE]), CFG_SIZE);

	if((cfg_msg.addr + cfg_msg.len) > g_flash_size)
	{
		ACK_msg.data_len = 0;
		ACK_msg.status = STATUS_ADDR_ERROR;
		ACK_msg.CRC8 = uboot_mesage_check((unsigned char*)&ACK_msg, ACK_SIZE - 1);
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}
	unsigned char hash[32] = { 0 };
	hau_hash_sha_256((void*)(0x8000000 + cfg_msg.addr), cfg_msg.len, hash);
	//uint8_t buffer[0x1000] = { 0 };
	//mbedtls_sha256_context ctx;
	//
	//mbedtls_sha256_init(&ctx);
	//mbedtls_sha256_starts(&ctx, false);
	//
	//uint32_t addr = cfg_msg.addr;
	//uint32_t remaining = cfg_msg.len;
	//while(remaining > 0)
	//{
	//	uint32_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
	//	//flash_read(addr, &buffer, chunk);
	//	memcpy(&buffer, (void*)(0x8000000 + addr), chunk);
	//	mbedtls_sha256_update(&ctx, (const unsigned char*)&buffer, chunk);
	//	addr += chunk;
	//	remaining -= chunk;
	//}
	//
	//mbedtls_sha256_finish(&ctx, (unsigned char*)&hash);

	memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);
	memcpy(&cmd_data_buf[HEAD_SIZE], &hash, 32);
	cmd_data_buf[HEAD_SIZE + 32] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + 32 + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + 32 + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + 32 + 2);
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
	sys_udelay(1000);
	flash_erase(cfg_msg.addr, cfg_msg.len);

	mz_stream stream;
	memset(&stream, 0, sizeof(stream));
	if(mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
	{
		ACK_msg.status = STATUS_ERROR;
		uart_write((unsigned char*)&ACK_msg, ACK_SIZE);
		return;
	}

	uart_put_char(stub_uart, CRC_MODE);

	flash_offset = cfg_msg.addr;

	for(;;)
	{
		uint32_t data_size = 0;

		if(uart_getc(stub_uart, &header[0], 3333) != 0)
		{
			uart_put_char(stub_uart, CRC_MODE);
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

					flash_write(flash_offset, cmd_data_buf, produced);
					flash_offset += produced;
				}

				if(status == MZ_STREAM_END) break;

				if(status != MZ_OK && status != MZ_BUF_ERROR)
					goto abort_decompression;
			}

			mz_inflateEnd(&stream);

			uart_put_char(stub_uart, ACK);
			return;
		}

		if(header[0] != STX && header[0] != SOH)
		{
			uart_put_char(stub_uart, NAK);
			continue;
		}

		data_size = (header[0] == STX) ? XMODEM_BLOCK_SIZE_1K : XMODEM_BLOCK_SIZE_128;

		uart_getc(stub_uart, &header[1], 10000);
		uart_getc(stub_uart, &header[2], 10000);

		if((header[1] + header[2]) != 0xFF)
		{
			uart_put_char(stub_uart, NAK);
			continue;
		}

		for(int i = 0; i < data_size; i++)
			uart_getc(stub_uart, &data[i], 20000);

		uart_getc(stub_uart, &crc_bytes[0], 10000);
		uart_getc(stub_uart, &crc_bytes[1], 10000);

		crc_recv = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
		crc_calc = crc16_ccitt(data, data_size);

		if(crc_recv != crc_calc)
		{
			uart_put_char(stub_uart, NAK);
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

				flash_write(flash_offset, cmd_data_buf, produced);
				flash_offset += produced;
			}

			if(status == MZ_STREAM_END) break;

			if(status != MZ_OK && status != MZ_BUF_ERROR)
				goto abort_decompression;
		}

		uart_put_char(stub_uart, ACK);
	}

abort_decompression:
	mz_inflateEnd(&stream);

	uart_put_char(stub_uart, CAN);
	uart_put_char(stub_uart, CAN);
}

#define RF_EFUSE_BASE ((volatile uint8_t *)0x40022200)

void rf_efuse_map_get(uint8_t map[64])
{
	uint8_t raw[64];

	for(int i = 0; i < 64; i++)
		raw[i] = RF_EFUSE_BASE[i];

	memset(map, 0, 64);

	int pos = 0;

	while(pos < 64)
	{
		uint8_t hdr = raw[pos++];

		if(hdr == 0xFF)
			break;

		uint8_t block = hdr >> 4;
		uint8_t bitmap = hdr & 0x0F;

		for(int i = 0; i < 4; i++)
		{
			if(bitmap & (1U << i))
			{
				if(pos >= 64)
					return;

				uint8_t index = block * 4 + i;

				if(index < 64)
					map[index] = raw[pos++];
			}
		}
	}
}

void uboot_read_efuse()
{
	ACK_msg.magic = ACK_MAGIC;
	ACK_msg.type = 0x99;
	ACK_msg.data_len = 64;

	memcpy(cmd_data_buf, &ACK_msg, HEAD_SIZE);
	rf_efuse_map_get(cmd_data_buf + HEAD_SIZE);

	cmd_data_buf[HEAD_SIZE + ACK_msg.data_len] = STATUS_SUCCESS;
	cmd_data_buf[HEAD_SIZE + ACK_msg.data_len + 1] = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + ACK_msg.data_len + 1);
	uart_write((unsigned char*)cmd_data_buf, HEAD_SIZE + ACK_msg.data_len + 2);
}

int uart_cmd_parser(void)
{
	unsigned int i = 0;
	unsigned char resp = 0;
	unsigned char CRC8 = 0;
	unsigned char buf[HEAD_SIZE] = { 0 };
	struct message_rec_head rec_head;

	unsigned char cfgbuf[CFG_SIZE] = { 0 };

	uart_fifo_reset(stub_uart);

	do
	{
		memset(buf, 0, HEAD_SIZE);
		for(i = 0; i < HEAD_SIZE; ++i)
		{
			while(RESET == usart_flag_get(stub_uart, USART_FLAG_TC));
			resp = uart_getc(stub_uart, (unsigned char*)&buf[i], 0xFFFFFFFF);
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

		//MEMSET(cmd_data_buf,0,CMD_DATA_MAX_LEN);
		memcpy(cmd_data_buf, buf, HEAD_SIZE);  // last buf, save 4 bytes head data
		memcpy(&rec_head, buf, HEAD_SIZE);  // data head, save 4 bytes to rec_head, and parser

		// check HEAD STATE
		if(buf[1] <= 0x9F)
		{
			for(i = 0; i < rec_head.data_len; i++)  // parser buf 4 bytes data, can get data_len.
			{
				// receive data_len data to cfgbuf
				resp = uart_getc(stub_uart, (unsigned char*)&cfgbuf[i], 0xFFFFFFFF);
				if(resp < 0)
				{
					resp = MSG_ERR;
					return resp;
				}
			}
			memcpy(&(cmd_data_buf[HEAD_SIZE]), cfgbuf, rec_head.data_len);  // last buf, save 8 bytes data

			// CRC8
			resp = uart_getc(stub_uart, (unsigned char*)&cmd_data_buf[HEAD_SIZE + rec_head.data_len], 0xFFFFFFFF);
			if(resp < 0)
			{
				resp = MSG_ERR;
				return resp;
			}
			CRC8 = uboot_mesage_check((unsigned char*)cmd_data_buf, HEAD_SIZE + rec_head.data_len);
			if(cmd_data_buf[HEAD_SIZE + rec_head.data_len] != CRC8)
			{
				rec_head.type = STATE_ERR;  // crc8 error
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
			case STATE_FLASH_ERASE:  // 0x04
				uboot_flash_erase_handle(&cmd_data_buf);
				break;
			case STATE_FLASH_CHIPERASE:  // 0x05
				uboot_flash_chiperase_handle(&cmd_data_buf);
				break;
			case STATE_BOUND:  // 0x07
				uboot_buad();
				break;
			case 0x09:
				uboot_flash_sha256(&cmd_data_buf);
				break;
			case 0x91:
				uboot_flash_xmodem_dl(&cmd_data_buf);
				break;
			case 0x92:
				uboot_flash_xmodem_ul(false, &cmd_data_buf);
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
