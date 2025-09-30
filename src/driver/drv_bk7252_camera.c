
#include "../new_common.h"
#include "../new_pins.h"
#include "../new_cfg.h"
// Commands register, execution API and cmd tokenizer
#include "../cmnds/cmd_public.h"
#include "../cmnds/cmd_local.h"
#include "../logging/logging.h"

#if PLATFORM_BK7252 || PLATFORM_BK7252N

#include "net.h"
#include "typedef.h"
#include "arm_arch.h"
#include "uart_pub.h"
#include "rtos_pub.h"
#include "error.h"
#include "video_transfer.h"
#include "i2c_pub.h"
#include "jpeg_encoder_pub.h"

#define MJPEG_BOUNDARY "boundarydonotcross"
#define MAX_BUF_SIZE    40*1024
static int  g_mjpeg_stop = 0;
static char g_send_buf[1024];
static beken_thread_t thread;

int send_first_response(int client)
{
    g_send_buf[0] = 0;

    snprintf(g_send_buf, 1024,
        "HTTP/1.0 200 OK\r\n"
        "Connection: close\r\n"
        "Server: MJPG-Streamer/0.2\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0,"
        " post-check=0, max-age=0\r\n"
        "Pragma: no-cache\r\n"
        "Expires: Mon, 3 Jan 2000 12:34:56 GMT\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary="
        MJPEG_BOUNDARY "\r\n"
        "\r\n"
        "--" MJPEG_BOUNDARY "\r\n");
    if(send(client, g_send_buf, strlen(g_send_buf), 0) < 0)
    {
        close(client);
        return -1;
    }

    return 0;
}

int mjpeg_send_stream(int client, void* data, int size)
{
    g_send_buf[0] = 0;

    if(!g_mjpeg_stop)
    {
        snprintf(g_send_buf, 1024,
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %d\r\n"
            "\r\n", size);
        if(send(client, g_send_buf, strlen(g_send_buf), 0) < 0)
        {
            close(client);
            return -1;
        }

        if(send(client, data, size, 0) < 0)
        {
            close(client);
            return -1;
        }

        g_send_buf[0] = 0;
        snprintf(g_send_buf, 1024, "\r\n--" MJPEG_BOUNDARY "\r\n");
        if(send(client, g_send_buf, strlen(g_send_buf), 0) < 0)
        {
            close(client);
            return -1;
        }

        return 0;
    }

    return -1;
}

void mjpeg_server_thread(void* arg)
{
    int on;
    int srv_sock = -1;
    int fream_length = 0;
    struct sockaddr_in addr;
    socklen_t sock_len = sizeof(struct sockaddr_in);

    //int bufsz = 50 * 1024;
    uint8_t* buf = (uint8_t*)malloc(MAX_BUF_SIZE);

    if(!buf)
    {
        bk_printf("no buffer yet!\n");
        return;
    }


    srv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(srv_sock < 0)
    {
        bk_printf("mjpeg_server: create server socket failed due to (%s)\n",
            strerror(errno));
        goto exit;
    }

    bzero(&addr, sock_len);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(7070);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* ignore "socket already in use" errors */
    on = 1;
    lwip_setsockopt(srv_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    lwip_setsockopt(srv_sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    if(bind(srv_sock, (struct sockaddr*)&addr, sock_len) != 0)
    {
        bk_printf("mjpeg_server: bind() failed due to (%s)\n",
            strerror(errno));
        goto exit;
    }

    if(listen(srv_sock, 2) != 0)
    {
        bk_printf("mjpeg_server: listen() failed due to (%s)\n",
            strerror(errno));
        goto exit;
    }

    g_mjpeg_stop = 0;
    while(!g_mjpeg_stop)
    {
        struct sockaddr_in client_addr;
        int client = accept(srv_sock, (struct sockaddr*)&client_addr, &sock_len);
        if(client < 0)
            continue;

        bk_printf("mjpeg_server: client connected\n");
        if(send_first_response(client) < 0)
        {
            client = -1;
            continue;
        }

        while(1)
        {
            fream_length = 0;
            /* capture a jpeg frame */
            fream_length = video_buffer_read_frame(buf, MAX_BUF_SIZE, NULL, 1000);
            bk_printf("len:%d\r\n",fream_length);
            if(fream_length != 0)
            {
                /* send out this frame */
                if(mjpeg_send_stream(client, (void*)buf, fream_length) < 0)
                {
                    bk_printf("client disconnected!\n");
                    break;
                }
            }
            if(g_mjpeg_stop) break;
            taskYIELD();
        }
        taskYIELD();
    }

exit:
    if(srv_sock >= 0)
        close(srv_sock);
    if(buf)
    {
        free(buf);
        buf = NULL;
    }
}

void scan_camera_sensors()
{

    DJPEG_DESC_ST ejpeg_cfg;

    UINT32 status;

    DD_HANDLE ejpeg_hdl = ddev_open(EJPEG_DEV_NAME, &status, (UINT32)&ejpeg_cfg);

    bk_printf("open EJPEG %p\r\n", ejpeg_hdl);
    bk_printf("status: %d\r\n", status);

    UINT32 i2c2_trans_mode = (0 & (~I2C2_MSG_WORK_MODE_MS_BIT)		// master
        & (~I2C2_MSG_WORK_MODE_AL_BIT)) 	// 7bit address
        | (I2C2_MSG_WORK_MODE_IA_BIT);

    //UINT32 oflag = 0;

    DD_HANDLE i2c_hdl = ddev_open(I2C2_DEV_NAME, &status, i2c2_trans_mode);
    bk_printf("open I2C2\r\n");
    bk_printf("status: %d\r\n", status);

    unsigned char data;
    I2C_OP_ST i2c_operater;


    //status = ddev_write(i2c_hdl, (char *)&data, 1, (UINT32)&i2c_operater);


    //i2c_operater.addr_width = ADDR_WIDTH_8;


    for(int i = 0; i < 128; i++)
    {

        i2c_operater.salve_id = (i << 1) | 1;

        i2c_operater.op_addr = 0x0F0;

        data = 0x00;

        //status = ddev_write(i2c_hdl, (char *) data, 1, (UINT32)&i2c_operater);

        //os_printf("\nREAD -> Status: %d, Data: %d %d %d %d", status, data[0], data[1], data[2], data[3]);

        status = ddev_read(i2c_hdl, (char*)&data, 1, (UINT32)&i2c_operater);

        bk_printf("READ -> Status: %d, Device: %x, Addr: %x, Data: %x \n\n", status, i2c_operater.salve_id, i2c_operater.op_addr, data);

    }


}

void BK_Cam_Init(void)
{
    scan_camera_sensors();
    video_buffer_open();
    rtos_create_thread(thread, 20, "jpeg_stream", mjpeg_server_thread, 2048, NULL);
}

void BK_Cam_Deinit(void)
{
    g_mjpeg_stop = 0;
    rtos_delete_thread(thread);
    video_transfer_deinit();
}

void BK_Cam_RunEverySecond(void)
{

}

#endif