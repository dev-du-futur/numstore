Q ?= @
CC = arm-none-eabi-gcc
BUILD_DIR = output
NWLINK = nwlink.cmd
LINK_GC = 1
LTO = 1

define object_for
$(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(basename $(1))))
endef

src = $(addprefix src/,\
  main.c \
  sbrk.c \
  nwai.c \
  helpers.c \
)

CFLAGS = -std=gnu99
CFLAGS += $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -O2 -ffast-math -Wall -mfp16-format=ieee
CFLAGS += -ggdb
LDFLAGS = $(shell $(NWLINK) eadk-ldflags-device)
LDFLAGS += --specs=nano.specs
# LDFLAGS += --specs=nosys.specs # Alternatively, use full-fledged newlib

ifeq ($(LINK_GC),1)
CFLAGS += -fdata-sections -ffunction-sections
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -Wl,-u,_sbrk
LDFLAGS += -Wl,--gc-sections
endif

ifeq ($(LTO),1)
CFLAGS += -flto -fno-fat-lto-objects
CFLAGS += -fwhole-program
CFLAGS += -fvisibility=internal
LDFLAGS += -flinker-output=nolto-rel
endif

.PHONY: build
build: $(BUILD_DIR)/app.nwa

.PHONY: check
check: $(BUILD_DIR)/app.bin

.PHONY: run
run: $(BUILD_DIR)/app.nwa model.bin
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa --external-data model.bin $<

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.nwa model.bin
	@echo "BIN     $@"
	$(Q) $(NWLINK) nwa-bin --external-data model.bin $< $@

$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.nwa model.bin
	@echo "ELF     $@"
	$(Q) $(NWLINK) nwa-elf --external-data model.bin $< $@

$(BUILD_DIR)/app.nwa: $(call object_for,$(src)) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@

# every object also depends on the headers, so a header change rebuilds everything
$(addprefix $(BUILD_DIR)/,%.o): %.c $(wildcard src/*.h) | $(BUILD_DIR)
	@echo "CC      $<"
	$(Q) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/icon.o: src/icon.png
	@echo "ICON    $<"
	$(Q) $(NWLINK) png-icon-o $< $@

.PRECIOUS: $(BUILD_DIR)
$(BUILD_DIR):
	$(Q) mkdir -p $@/src

.PHONY: clean
clean:
	@echo "CLEAN"
	$(Q) rm -rf $(BUILD_DIR)
