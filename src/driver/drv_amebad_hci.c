#include "../new_common.h"
#include "../new_pins.h"
#include "../new_cfg.h"
#include "../quicktick.h"
#include "../logging/logging.h"
#include "errno.h"
#include <lwip/sockets.h>
#include "serial_api.h"
#include "rtl8721d_uart.h"

#if ENABLE_DRIVER_AMEBAD_HCI

#define DEFAULT_BUF_SIZE		512
#define DEFAULT_HCI_TCP_PORT	8721
#define INVALID_SOCK			-1
#ifndef HCI_DEBUG
#define HCI_DEBUG				1
#endif
#if HCI_DEBUG
#define STACK_SIZE				8192
#else
#define STACK_SIZE				3584
#endif

static byte* g_recvBuf = NULL;
static int g_recvBufSize = DEFAULT_BUF_SIZE;
static int g_recvBufIn = 0;
static int g_recvBufOut = 0;
static int listen_sock = INVALID_SOCK;
static int client_sock = INVALID_SOCK;
static xTaskHandle g_start_thread = NULL;
static xTaskHandle g_trx_thread = NULL;
static xTaskHandle g_rx_thread = NULL;
static xTaskHandle g_tx_thread = NULL;
static bool rx_closed, tx_closed;
static byte* g_utcpBuf = 0;

void Start_HCI_TCP(void* arg);

static void HCI_InitReceiveRingBuffer(int size)
{
	if(g_recvBuf)
		free(g_recvBuf);
	g_recvBuf = (byte*)malloc(size);
	memset(g_recvBuf, 0, size);
	g_recvBufSize = size;
	g_recvBufIn = 0;
	g_recvBufOut = 0;
}

static int HCI_GetDataSize()
{
	return (g_recvBufIn >= g_recvBufOut
		? g_recvBufIn - g_recvBufOut
		: g_recvBufIn + (g_recvBufSize - g_recvBufOut));
}

static byte HCI_GetByte(int idx)
{
	return g_recvBuf[(g_recvBufOut + idx) % g_recvBufSize];
}

static void HCI_ConsumeBytes(int idx)
{
	g_recvBufOut += idx;
	g_recvBufOut %= g_recvBufSize;
}

static void HCI_AppendByteToReceiveRingBuffer(int rc)
{
	if(HCI_GetDataSize() < (g_recvBufSize - 1))
	{
		g_recvBuf[g_recvBufIn++] = rc;
		g_recvBufIn %= g_recvBufSize;
	}
	if(g_recvBufIn == g_recvBufOut)
	{
		g_recvBufOut++;
		g_recvBufOut %= g_recvBufSize;
	}
}

uint8_t hci_cb()
{
	uint8_t buffer[DEFAULT_BUF_SIZE];
	uint16_t len = hci_tp_recv(buffer, g_recvBufSize);
	for(int i = 0; i < len; i++)
	{
		HCI_AppendByteToReceiveRingBuffer(buffer[i]);
	}
	return 0;
}

static void HCI_TX_Thd(void* param)
{
	int client_fd = *(int*)param;

	while(1)
	{
		int ret = 0;
		int delay = 0;
		memset(g_utcpBuf, 0, g_recvBufSize);
		int len = HCI_GetDataSize();

		if(client_fd == INVALID_SOCK) goto exit;
		while(len < g_recvBufSize && delay < 5)
		{
			rtos_delay_milliseconds(1);
			len = HCI_GetDataSize();
			delay++;
		}

		if(len > 0)
		{
			for(int i = 0; i < len; i++)
			{
				g_utcpBuf[i] = HCI_GetByte(i);
			}
			HCI_ConsumeBytes(len);
#if HCI_DEBUG
			char data[len * 2];
			char* p = data;
			for(int i = 0; i < len; i++)
			{
				sprintf(p, "%02X", g_utcpBuf[i]);
				p += 2;
			}
			ADDLOG_EXTRADEBUG(LOG_FEATURE_DRV, "%d bytes HCI>TCP: %s", len, data);
#endif
			ret = send(client_fd, g_utcpBuf, len, 0);
		}
		else
		{
			if(rx_closed)
			{
				goto exit;
			}
			continue;
		}

		if(ret <= 0)
			goto exit;

		rtos_delay_milliseconds(5);
	}

exit:
	ADDLOG_DEBUG(LOG_FEATURE_DRV, "HCI_TX_Thd closed");
	tx_closed = true;
	rtos_suspend_thread(NULL);
}

static void HCI_RX_Thd(void* param)
{
	int client_fd = *(int*)param;
	unsigned char buffer[1024];

	while(1)
	{
		int ret = 0;

		if(client_fd == INVALID_SOCK) goto exit;
		ret = recv(client_fd, buffer, sizeof(buffer), 0);
		if(ret > 0)
		{
#if HCI_DEBUG
			char data[ret * 2];
			char* p = data;
			for(int i = 0; i < ret; i++)
			{
				sprintf(p, "%02X", buffer[i]);
				p += 2;
			}
			ADDLOG_EXTRADEBUG(LOG_FEATURE_DRV, "%d bytes TCP->HCI: %s", ret, data);
#endif
			hci_tp_send(buffer, ret, NULL);
		}
		else if(tx_closed)
		{
			goto exit;
		}

		// ret == -1 and socket error == EAGAIN when no data received for nonblocking
		if((ret == -1) && (errno == EAGAIN))
			continue;
		else if(ret <= 0)
		{
			ADDLOG_DEBUG(LOG_FEATURE_DRV, "ret: %i, errno: %i", ret, errno);
			goto exit;
		}

		rtos_delay_milliseconds(5);
	}

exit:
	ADDLOG_DEBUG(LOG_FEATURE_DRV, "HCI_RX_Thd closed");
	rx_closed = true;
	rtos_suspend_thread(NULL);
}

void HCI_TCP_TRX_Thread()
{
	OSStatus err = kNoErr;
	int reuse = 1;
	struct sockaddr_in server_addr =
	{
		.sin_family = AF_INET,
		.sin_addr =
		{
			.s_addr = INADDR_ANY,
		},
		.sin_port = htons(DEFAULT_HCI_TCP_PORT),
	};

	if(listen_sock != INVALID_SOCK) close(listen_sock);
	if(client_sock != INVALID_SOCK) close(client_sock);

	listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(listen_sock < 0)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "Unable to create socket");
		goto error;
	}
	int flags = fcntl(listen_sock, F_GETFL, 0);
	if(fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "Unable to set socket non blocking");
		goto error;
	}

	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	err = bind(listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if(err != 0)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "Socket unable to bind");
		goto error;
	}
	err = listen(listen_sock, 2);
	if(err != 0)
	{
		ADDLOG_ERROR(LOG_FEATURE_HTTP, "Error occurred during listen");
		goto error;
	}

	while(1)
	{
		struct sockaddr_storage source_addr;
		socklen_t addr_len = sizeof(source_addr);
		client_sock = accept(listen_sock, (struct sockaddr*)&source_addr, &addr_len);
		if(client_sock != INVALID_SOCK)
		{
			rx_closed = true;
			tx_closed = true;

			if(g_tx_thread != NULL)
			{
				rtos_delete_thread(&g_tx_thread);
			}
			err = rtos_create_thread(&g_tx_thread, BEKEN_APPLICATION_PRIORITY - 1,
				"HCI_TX_Thd",
				(beken_thread_function_t)HCI_TX_Thd,
				STACK_SIZE,
				(beken_thread_arg_t)&client_sock);
			if(err != kNoErr)
			{
				ADDLOG_ERROR(LOG_FEATURE_DRV, "create \"HCI_TX_Thd\" thread failed with %i!", err);
			}
			else tx_closed = false;

			//rtos_delay_milliseconds(10);

			if(g_rx_thread != NULL)
			{
				rtos_delete_thread(&g_rx_thread);
			}
			err = rtos_create_thread(&g_rx_thread, BEKEN_APPLICATION_PRIORITY - 1,
				"HCI_RX_Thd",
				(beken_thread_function_t)HCI_RX_Thd,
				STACK_SIZE,
				(beken_thread_arg_t)&client_sock);
			if(err != kNoErr)
			{
				ADDLOG_ERROR(LOG_FEATURE_DRV, "create \"HCI_RX_Thd\" thread failed with %i!", err);
			}
			else rx_closed = false;

			while(1)
			{
				if(tx_closed && rx_closed)
				{
					close(client_sock);
					ADDLOG_DEBUG(LOG_FEATURE_DRV, "HCI TCP connection closed", err);
					break;
				}
				else
				{
					if(!Main_HasWiFiConnected()) goto error;
					rtos_delay_milliseconds(10);
				}
			}
		}
		rtos_delay_milliseconds(10);
	}

error:
	ADDLOG_ERROR(LOG_FEATURE_DRV, "HCI TCP Error");

	if(g_start_thread != NULL)
	{
		rtos_delete_thread(&g_start_thread);
	}
	err = rtos_create_thread(&g_start_thread, BEKEN_APPLICATION_PRIORITY,
		"HCI TCP Restart",
		(beken_thread_function_t)Start_HCI_TCP,
		0x800,
		(beken_thread_arg_t)0);
	if(err != kNoErr)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "create \"HCI TCP Restart\" thread failed with %i!\r\n", err);
	}
}

void Start_HCI_TCP(void* arg)
{
	HCI_Deinit();

	g_utcpBuf = (byte*)os_malloc(g_recvBufSize);

	OSStatus err = rtos_create_thread(&g_trx_thread, BEKEN_APPLICATION_PRIORITY,
		"HCI_TCP_TRX",
		(beken_thread_function_t)HCI_TCP_TRX_Thread,
		0x800,
		(beken_thread_arg_t)0);
	if(err != kNoErr)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "create \"HCI_TCP_TRX\" thread failed with %i!\r\n", err);
	}
	rtos_suspend_thread(NULL);
}

static void empty_cb(bool)
{

}

void HCI_Init()
{
	HCI_InitReceiveRingBuffer(g_recvBufSize * 2);

	if(g_start_thread != NULL)
	{
		rtos_delete_thread(&g_start_thread);
	}
	OSStatus err = rtos_create_thread(&g_start_thread, BEKEN_APPLICATION_PRIORITY,
		"HCI_TCP",
		(beken_thread_function_t)Start_HCI_TCP,
		0x800,
		(beken_thread_arg_t)0);
	if(err != kNoErr)
	{
		ADDLOG_ERROR(LOG_FEATURE_DRV, "create \"HCI_TCP\" thread failed with %i!", err);
	}
	hci_tp_open(empty_cb, hci_cb);
}

void HCI_Deinit()
{
	if(g_trx_thread != NULL)
	{
		rtos_delete_thread(&g_trx_thread);
		g_trx_thread = NULL;
	}
	if(g_rx_thread != NULL)
	{
		rx_closed = true;
		rtos_delete_thread(&g_rx_thread);
		g_rx_thread = NULL;
	}
	if(g_tx_thread != NULL)
	{
		tx_closed = true;
		rtos_delete_thread(&g_tx_thread);
		g_tx_thread = NULL;
	}
	if(g_utcpBuf) free(g_utcpBuf);

	if(listen_sock != INVALID_SOCK) close(listen_sock);
	if(client_sock != INVALID_SOCK) close(client_sock);
	hci_tp_del(1);
}

#endif