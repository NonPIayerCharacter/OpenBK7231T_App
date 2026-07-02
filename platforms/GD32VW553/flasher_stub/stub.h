#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "gd32vw55x_usart.h"


/*--------------------------------------------------------------------------
* 	                                        	Macros
--------------------------------------------------------------------------*/

//----------------------------- FRAME FORMAT ---------------------------------
// The format can be adjusted to fit your particular application needs
//REQ frame
// ,-------+------+-------------+-------+----------,
// | MAGIC | TYPE |  DATA_LEN	| DATA	|	CRC8   |
// |   1   |   1  | 	 2		|	N	|	  1    | <- size (bytes)
// '-------+------+-------------+-------+----------'

//ACK frame
// ,-------+------+-------------+-------+----------+----------,
// | MAGIC | TYPE |  DATA_LEN	| DATA	|  status  |   CRC8   |
// |   1   |   1  | 	 2		|	N	|	 1	   |	 1	  | <- size (bytes)
// '-------+------+-------------+-------+----------+----------'
/** magic */
#define MAGIC 						0xA5 // start of a frame
#define ACK_MAGIC 					0x5A 

/** massage type */
#define STATE_ERR               	0xFF
#define STATE_SYN               	0x00
#define STATE_RAM_DOWNLOAD      	0x01
#define STATE_FLASH_DOWNLOAD    	0x02
#define STATE_FLASH_UPLOAD      	0x03
#define STATE_FLASH_ERASE       	0x04
#define STATE_FLASH_CHIPERASE   	0x05
#define STATE_RUN					0x06
#define STATE_BOUND					0x07
#define STATE_MAX					0x08

/** status */
#define STATUS_SUCCESS				0x00
#define STATUS_ERROR				0x01
#define STATUS_ADDR_ERROR			0x02
#define STATUS_TYPE_ERROR			0x03
#define STATUS_LEN_ERROR			0x04
#define STATUS_CRC_ERROR			0x05


/** RESPONSE */
#define RESPONSE_FAIL				0xFF
#define RESPONSE_OK					0x00
#define RESPONSE_SYNC_BOOTROM		0x01
#define RESPONSE_SYNC_SBL			0x02

/** ACK message define */
#define ACK_OK  0x00
#define ACK_ERR 0x01

/** message response status */
#define MSG_OK 0x00
#define MSG_ERR -1


/** sync */
#define SYNC_REQUEST_VALUE			0x73796E63	/* sync */ 
#define SYNC_REQUEST_SIZE			0x04
#define SYNC_REQUEST_TIMEOUT		120  // 2 min


/** all size */
#define HEAD_SIZE					4
#define CFG_SIZE					8
#define ACK_SIZE					6
#define CMD_DATA_MAX_LEN 			(4 + 1 + 1024*8 + 2) //max command data length 64K
#define RESPONSE_SIZE				0x01
#define LOAD_MAX_SIZE_BIG			0x10000

struct sburner_cmd
{
	unsigned int	msg_type;
	unsigned int	arg0;
	unsigned int	arg1;
};

struct message_rec_head
{
	unsigned char			magic;
	unsigned char			type;
	unsigned short			data_len;
	unsigned int			run_addr;
	unsigned char			CRC8;
};

#define MESSAGE_REC_SIZE			sizeof(struct message_rec_head)

struct message_ack_head
{
	unsigned char			magic;
	unsigned char			type;
	unsigned short			data_len;
	unsigned char			status;
	unsigned char			CRC8;
};
#define MESSAGE_ACK_SIZE			sizeof(struct message_ack_head)

struct load_cfg_msg
{
	unsigned int	addr;
	unsigned int	len;
} load_cfg_msg_t;
#define MESSAGE_LOAD_SIZE			sizeof(struct load_cfg_msg)

// pack the message_head
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

#define SPIFLASH_READY 				(1)
#define FLASH_SIZE_1_MB             (0x00100000)
#define FLASH_SIZE_2_MB             (0x00200000)
#define FLASH_SIZE_4_MB             (0x00400000)

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC_MODE 'C'
#define XMODEM_BLOCK_SIZE_1K 1024
