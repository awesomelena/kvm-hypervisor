CC = gcc

BUILD_DIR ?= build

# Dodatne opcije u odnosu na pocetni Makefile:
#   -mgeneral-regs-only            zabranjuje SSE u celom gost kodu (CR4.OSFXSR nije postavljen)
#   -mno-red-zone                  prekidna rutina ne sme da gazi red zonu prekinutog koda
#   -fno-stack-protector           nema podrske za stack protector u freestanding okruzenju
#   -fcf-protection=none           bez CET-a - sprecava .note.gnu.property sekciju u slici
#   -fno-asynchronous-unwind-tables bez .eh_frame sekcije u slici
CFLAGS = -m64 -ffreestanding -fno-pic -mgeneral-regs-only -mno-red-zone \
         -fno-stack-protector -fcf-protection=none -fno-asynchronous-unwind-tables

# Svaki src/main*.c postaje posebna gost slika (main.c -> guest.img,
# main_xyz.c -> guest_xyz.img), a ostali fajlovi iz src/ su zajednicki.
SRCS  = $(wildcard src/*.c)
MAINS = $(wildcard src/main*.c)
COMMON_SRCS = $(filter-out $(MAINS),$(SRCS))
COMMON_OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)

IMGS = $(patsubst src/main%.c,$(BUILD_DIR)/guest%.img,$(MAINS))

.PHONY: all clean
.SECONDARY: $(OBJS)

all: $(BUILD_DIR) $(IMGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinc -MMD -MP -c -o $@ $<

$(BUILD_DIR)/guest.img: $(BUILD_DIR)/main.o $(COMMON_OBJS) guest.ld
	ld -T guest.ld $(BUILD_DIR)/main.o $(COMMON_OBJS) -o $@

$(BUILD_DIR)/guest_%.img: $(BUILD_DIR)/main_%.o $(COMMON_OBJS) guest.ld
	ld -T guest.ld $(BUILD_DIR)/main_$*.o $(COMMON_OBJS) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
