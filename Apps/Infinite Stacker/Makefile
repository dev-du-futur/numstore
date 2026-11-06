# Build from inside this folder (its name has a space; every path below is relative): make build / make dist
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
  game.c \
  render.c \
)

CFLAGS = -std=gnu99
CFLAGS += $(shell $(NWLINK) eadk-cflags-device)
CFLAGS += -O2 -ffast-math -Wall -Wshadow=local
CFLAGS += -ggdb
LDFLAGS = $(shell $(NWLINK) eadk-ldflags-device)
LDFLAGS += --specs=nano.specs

ifeq ($(LINK_GC),1)
CFLAGS += -fdata-sections -ffunction-sections
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level
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
run: $(BUILD_DIR)/app.nwa
	@echo "INSTALL $<"
	$(Q) $(NWLINK) install-nwa $<

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.nwa
	@echo "BIN     $@"
	$(Q) $(NWLINK) nwa-bin $< $@

$(BUILD_DIR)/%.elf: $(BUILD_DIR)/%.nwa
	@echo "ELF     $@"
	$(Q) $(NWLINK) nwa-elf $< $@

$(BUILD_DIR)/app.nwa: $(call object_for,$(src)) $(BUILD_DIR)/icon.o
	@echo "LD      $@"
	$(Q) $(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@

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

# PC check: plays runs with a bot, checks the partial redraws against full ones, saves frames to host/out
.PHONY: host
host:
	$(if $(wildcard host/out),,$(Q) mkdir -p host/out)
	$(Q) gcc -O2 -std=gnu99 -Wall -Ihost -o host/stacker_test host/test.c host/eadk_host.c src/game.c src/render.c -lm
	$(Q) ./host/stacker_test host/out

# the file Numstore ships (debug info stripped)
.PHONY: dist
dist: $(BUILD_DIR)/app.nwa
	$(if $(wildcard dist),,$(Q) mkdir dist)
	$(Q) arm-none-eabi-strip --strip-debug $< -o dist/infinite-stacker.nwa
	@echo "DIST    dist/infinite-stacker.nwa"
