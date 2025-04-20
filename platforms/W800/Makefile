TOP_DIR = ..

$(info    TOP_DIR is $(TOP_DIR)) 
sinclude $(TOP_DIR)/tools/w800/conf.mk

ifndef PDIR
GEN_LIBS = libuser$(LIB_EXT)
endif

#DEFINES +=

_SHARED_APP = $(TOP_DIR)/sharedAppContainer/sharedApp/src

$(info    _SHARED_APP is $(_SHARED_APP)) 

CSRCS += $(_SHARED_APP)/hal/w800/hal_adc_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_flashConfig_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_flashVars_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_generic_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_main_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_pins_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_wifi_w800.c
CSRCS += $(_SHARED_APP)/hal/w800/hal_uart_w800.c
CSRCS += main.c

OBK_SRCS = $(_SHARED_APP)/
include $(_SHARED_APP)/../platforms/drivers.mk
include $(_SHARED_APP)/../platforms/obk_main.mk
CSRCS += $(OBKM_SRC)
CSRCS += $(DRIVER_SRC)

CSRCS += $(TOP_DIR)/src/network/lwip2.0.3/apps/mqtt/mqtt.c

sinclude $(TOP_DIR)/tools/w800/rules.mk

INCLUDES := $(INCLUDES) -I $(PDIR)include

PDIR := ../$(PDIR)
sinclude $(PDIR)Makefile
