# Changelog
# Changed the variables to include the header file directory
# Added global var for the XTENSA tool root
#
# This make file still needs some work.
#
#
# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build
FW_BASE = firmware
ESPTOOL = tools/esptool.py


# name for the target project
TARGET		= app

# linker script used for the above linkier step
LD_SCRIPT	= eagle.app.v6.ld

# we create two different files for uploading into the flash
# these are the names and options to generate them
FW_1	= 0x00000
FW_2	= 0x40000

FLAVOR ?= release


#############################################################
# Select compile
#
ifeq ($(OS),Windows_NT)
# WIN32
# We are under windows.
	ifeq ($(XTENSA_CORE),lx106)
		# It is xcc
		AR = xt-ar
		CC = xt-xcc
		LD = xt-xcc
		NM = xt-nm
		CPP = xt-cpp
		OBJCOPY = xt-objcopy
		#MAKE = xt-make
		CCFLAGS += -Os --rename-section .text=.irom0.text --rename-section .literal=.irom0.literal
	else 
		# It is gcc, may be cygwin
		# Can we use -fdata-sections?
		CCFLAGS += -Os -ffunction-sections -fno-jump-tables
		AR = xtensa-lx106-elf-ar
		CC = xtensa-lx106-elf-gcc
		LD = xtensa-lx106-elf-gcc
		NM = xtensa-lx106-elf-nm
		CPP = xtensa-lx106-elf-cpp
		OBJCOPY = xtensa-lx106-elf-objcopy
	endif
	ESPPORT 	?= com1
	SDK_BASE	?= c:/Espressif/ESP8266_SDK
    ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
# ->AMD64
    endif
    ifeq ($(PROCESSOR_ARCHITECTURE),x86)
# ->IA32
    endif
else
# We are under other system, may be Linux. Assume using gcc.
	# Can we use -fdata-sections?
	ESPPORT ?= /dev/ttyUSB0
	SDK_BASE	?= /esptools/esp-open-sdk/sdk

	CCFLAGS += -Os -ffunction-sections -fno-jump-tables
	AR = xtensa-lx106-elf-ar
	CC = xtensa-lx106-elf-gcc
	LD = xtensa-lx106-elf-gcc
	NM = xtensa-lx106-elf-nm
	CPP = xtensa-lx106-elf-cpp
	OBJCOPY = xtensa-lx106-elf-objcopy
    UNAME_S := $(shell uname -s)

    ifeq ($(UNAME_S),Linux)
# LINUX
    endif
    ifeq ($(UNAME_S),Darwin)
# OSX
    endif
    UNAME_P := $(shell uname -p)
    ifeq ($(UNAME_P),x86_64)
# ->AMD64
    endif
    ifneq ($(filter %86,$(UNAME_P)),)
# ->IA32
    endif
    ifneq ($(filter arm%,$(UNAME_P)),)
# ->ARM
    endif
endif
#############################################################


# which modules (subdirectories) of the project to include in compiling
MODULES		= driver mqtt user
EXTRA_INCDIR    = include $(SDK_BASE)/../include

# libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal phy pp net80211 lwip wpa main ssl

# compiler flags using during compilation of source files
CFLAGS		= -Os -Wpointer-arith -Wundef -Werror -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH

# linker flags used to generate the main object file
LDFLAGS		= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static

ifeq ($(FLAVOR),debug)
    CFLAGS += -g -O0
    LDFLAGS += -g -O0
endif

ifeq ($(FLAVOR),release)
    CFLAGS += -g -O2
    LDFLAGS += -g -O2
endif



# various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json

####
#### no user configurable options below here
####
FW_TOOL		?= $(ESPTOOL)
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))

SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)
TARGET_OUT	:= $(addprefix $(BUILD_BASE)/,$(TARGET).out)

LD_SCRIPT	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_SCRIPT))

INCDIR	:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

FW_FILE_1	:= $(addprefix $(FW_BASE)/,$(FW_1).bin)
FW_FILE_2	:= $(addprefix $(FW_BASE)/,$(FW_2).bin)

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs $(TARGET_OUT) $(FW_FILE_1) $(FW_FILE_2)

$(FW_FILE_1): $(TARGET_OUT)
	$(vecho) "FW $@"
	$(ESPTOOL) elf2image $< -o $(FW_BASE)/
	
$(FW_FILE_2): $(TARGET_OUT)
	$(vecho) "FW $@"
	$(ESPTOOL) elf2image $< -o $(FW_BASE)/

$(TARGET_OUT): $(APP_AR)
	$(vecho) "LD $@"
	$(Q) $(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

firmware:
	$(Q) mkdir -p $@

flash: $(FW_FILE_1)  $(FW_FILE_2)
	$(ESPTOOL) -p $(ESPPORT) write_flash $(FW_1) $(FW_FILE_1) $(FW_2) $(FW_FILE_2)

test: flash
	screen $(ESPPORT) 115200

rebuild: clean all

clean:
	$(Q) rm -f $(APP_AR)
	$(Q) rm -f $(TARGET_OUT)
	$(Q) rm -rf $(BUILD_DIR)
	$(Q) rm -rf $(BUILD_BASE)
	$(Q) rm -f $(FW_FILE_1)
	$(Q) rm -f $(FW_FILE_2)
	$(Q) rm -rf $(FW_BASE)

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
