OBK_DIR = ../../../../..

CFLAGS +=  -DPLATFORM_RTL8710B -DPLATFORM_REALTEK -DUSER_SW_VER='"$(APP_VERSION)"'

INCLUDES += -I$(OBK_DIR)/libraries/easyflash/inc

SRC_C  += $(OBK_DIR)/platforms/RTL8710B/main.c
SRC_C  += $(OBK_DIR)/platforms/RTL8710B/stdlib.c

SRC_C  += $(OBK_DIR)/src/base64/base64.c
SRC_C  += $(OBK_DIR)/src/bitmessage/bitmessage_read.c
SRC_C  += $(OBK_DIR)/src/bitmessage/bitmessage_write.c
SRC_C  += $(OBK_DIR)/src/cJSON/cJSON.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_channels.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_eventHandlers.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_if.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_main.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_newLEDDriver_colors.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_newLEDDriver.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_repeatingEvents.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_script.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_simulatorOnly.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_tasmota.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_tcp.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_test.c
SRC_C  += $(OBK_DIR)/src/cmnds/cmd_tokenizer.c
SRC_C  += $(OBK_DIR)/src/devicegroups/deviceGroups_read.c
SRC_C  += $(OBK_DIR)/src/devicegroups/deviceGroups_util.c
SRC_C  += $(OBK_DIR)/src/devicegroups/deviceGroups_write.c
SRC_C  += $(OBK_DIR)/src/driver/drv_main.c
#SRC_C  += $(OBK_DIR)/src/hal/realtek/rtl8710b/hal_adc_rtl8710b.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/rtl8710b/hal_generic_rtl8710b.c
#SRC_C  += $(OBK_DIR)/src/hal/realtek/rtl8710b/hal_main_rtl8710b.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/rtl8710b/hal_uart_rtl8710b.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/rtl8710b/hal_pins_rtl8710b.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/hal_flashConfig_realtek.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/hal_flashVars_realtek.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/hal_pins_realtek.c
SRC_C  += $(OBK_DIR)/src/hal/realtek/hal_wifi_realtek.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_adc_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_flashConfig_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_flashVars_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_main_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_pins_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_wifi_generic.c
SRC_C  += $(OBK_DIR)/src/hal/generic/hal_uart_generic.c
SRC_C  += $(OBK_DIR)/src/httpserver/hass.c
SRC_C  += $(OBK_DIR)/src/httpserver/http_basic_auth.c
SRC_C  += $(OBK_DIR)/src/httpserver/http_fns.c
SRC_C  += $(OBK_DIR)/src/httpserver/http_tcp_server.c
SRC_C  += $(OBK_DIR)/src/httpserver/new_tcp_server.c
SRC_C  += $(OBK_DIR)/src/httpserver/json_interface.c
SRC_C  += $(OBK_DIR)/src/httpserver/new_http.c
SRC_C  += $(OBK_DIR)/src/httpserver/rest_interface.c
SRC_C  += $(OBK_DIR)/src/mqtt/new_mqtt_deduper.c
SRC_C  += $(OBK_DIR)/src/jsmn/jsmn.c
SRC_C  += $(OBK_DIR)/src/logging/logging.c
SRC_C  += $(OBK_DIR)/src/mqtt/new_mqtt.c
SRC_C  += $(OBK_DIR)/src/new_cfg.c
SRC_C  += $(OBK_DIR)/src/new_common.c
SRC_C  += $(OBK_DIR)/src/new_ping.c
SRC_C  += $(OBK_DIR)/src/new_pins.c
SRC_C  += $(OBK_DIR)/src/rgb2hsv.c
SRC_C  += $(OBK_DIR)/src/tiny_crc8.c
SRC_C  += $(OBK_DIR)/src/user_main.c
#SRC_C += $(OBK_DIR)/src/cmnds/cmd_send.c
SRC_C  += $(OBK_DIR)/src/driver/drv_adcButton.c
SRC_C  += $(OBK_DIR)/src/driver/drv_adcSmoother.c
SRC_C  += $(OBK_DIR)/src/driver/drv_aht2x.c
SRC_C  += $(OBK_DIR)/src/driver/drv_battery.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bl_shared.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bl0937.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bl0942.c
#SRC_C += $(OBK_DIR)/src/driver/drv_bmp280.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bmpi2c.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bp1658cj.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bp5758d.c
SRC_C  += $(OBK_DIR)/src/driver/drv_bridge_driver.c
SRC_C  += $(OBK_DIR)/src/driver/drv_chargingLimit.c
SRC_C  += $(OBK_DIR)/src/driver/drv_charts.c
SRC_C  += $(OBK_DIR)/src/driver/drv_cht8305.c
SRC_C  += $(OBK_DIR)/src/driver/drv_cse7766.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ddp.c
SRC_C  += $(OBK_DIR)/src/driver/drv_debouncer.c
SRC_C  += $(OBK_DIR)/src/driver/drv_dht_internal.c
SRC_C  += $(OBK_DIR)/src/driver/drv_dht.c
SRC_C  += $(OBK_DIR)/src/driver/drv_doorSensorWithDeepSleep.c
SRC_C  += $(OBK_DIR)/src/driver/drv_gn6932.c
SRC_C  += $(OBK_DIR)/src/driver/drv_hd2015.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ht16k33.c
SRC_C  += $(OBK_DIR)/src/driver/drv_httpButtons.c
SRC_C  += $(OBK_DIR)/src/driver/drv_hue.c
SRC_C  += $(OBK_DIR)/src/driver/drv_kp18058.c
SRC_C  += $(OBK_DIR)/src/driver/drv_kp18068.c
SRC_C  += $(OBK_DIR)/src/driver/drv_max72xx_clock.c
SRC_C  += $(OBK_DIR)/src/driver/drv_max72xx_internal.c
SRC_C  += $(OBK_DIR)/src/driver/drv_max72xx_single.c
SRC_C  += $(OBK_DIR)/src/driver/drv_mcp9808.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ntp_events.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ntp.c
SRC_C  += $(OBK_DIR)/src/driver/drv_pt6523.c
SRC_C  += $(OBK_DIR)/src/driver/drv_pwm_groups.c
SRC_C  += $(OBK_DIR)/src/driver/drv_pwmToggler.c
SRC_C  += $(OBK_DIR)/src/driver/drv_pwrCal.c
SRC_C  += $(OBK_DIR)/src/driver/drv_rn8209.c
SRC_C  += $(OBK_DIR)/src/driver/drv_sgp.c
SRC_C  += $(OBK_DIR)/src/driver/drv_shiftRegister.c
SRC_C  += $(OBK_DIR)/src/driver/drv_sht3x.c
#SRC_C += $(OBK_DIR)/src/driver/drv_sm15155e.c
#SRC_C += $(OBK_DIR)/src/driver/drv_sm16703P.c
SRC_C  += $(OBK_DIR)/src/driver/drv_sm2135.c
SRC_C  += $(OBK_DIR)/src/driver/drv_sm2235.c
SRC_C  += $(OBK_DIR)/src/driver/drv_soft_i2c.c
#SRC_C += $(OBK_DIR)/src/driver/drv_soft_spi.c
#SRC_C += $(OBK_DIR)/src/driver/drv_spi_flash.c
#SRC_C += $(OBK_DIR)/src/driver/drv_spi.c
#SRC_C += $(OBK_DIR)/src/driver/drv_spidma.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ssdp.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tasmotaDeviceGroups.c
SRC_C  += $(OBK_DIR)/src/driver/drv_test_drivers.c
SRC_C  += $(OBK_DIR)/src/driver/drv_textScroller.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tm_gn_display_shared.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tm1637.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tm1638.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tuyaMCU.c
SRC_C  += $(OBK_DIR)/src/driver/drv_tuyaMCUSensor.c
SRC_C  += $(OBK_DIR)/src/driver/drv_uart.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ucs1912.c
SRC_C  += $(OBK_DIR)/src/driver/drv_wemo.c
SRC_C  += $(OBK_DIR)/src/driver/drv_ds1820_simple.c
#SRC_C += $(OBK_DIR)/src/httpclient/http_client.c
#SRC_C += $(OBK_DIR)/src/httpclient/utils_net.c
#SRC_C += $(OBK_DIR)/src/httpclient/utils_timer.c
SRC_C  += $(OBK_DIR)/src/i2c/drv_i2c_lcd_pcf8574t.c
SRC_C  += $(OBK_DIR)/src/i2c/drv_i2c_main.c
SRC_C  += $(OBK_DIR)/src/i2c/drv_i2c_mcp23017.c
SRC_C  += $(OBK_DIR)/src/i2c/drv_i2c_tc74.c
SRC_C  += $(OBK_DIR)/src/littlefs/lfs_util.c
SRC_C  += $(OBK_DIR)/src/littlefs/lfs.c
SRC_C  += $(OBK_DIR)/src/littlefs/our_lfs.c
#SRC_C += $(OBK_DIR)/src/memory/memtest.c
#SRC_C += $(OBK_DIR)/src/ota/ota.c
#SRC_C += $(OBK_DIR)/src/sim/sim_uart.c


SRC_C += $(OBK_DIR)/libraries/easyflash/ports/ef_port.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/easyflash.c
#SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_cmd.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_env.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_env_legacy.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_env_legacy_wl.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_iap.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_log.c
SRC_C += $(OBK_DIR)/libraries/easyflash/src/ef_utils.c