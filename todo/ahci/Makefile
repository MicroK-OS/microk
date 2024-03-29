ARCH = x86_64

MODDIR = .
MODNAME = ahci

CC = $(ARCH)-elf-gcc
CPP = $(ARCH)-elf-g++
ASM = nasm
LD = $(ARCH)-elf-ld

CFLAGS = -ffreestanding       \
	 -fno-stack-protector \
	 -fno-omit-frame-pointer \
	 -fno-builtin-g       \
	 -fno-stack-check     \
	 -I ../../kernel/include    \
	 -m64                 \
	 -mabi=sysv           \
	 -mno-80387           \
	 -mno-mmx             \
	 -mno-sse             \
	 -mno-sse2            \
	 -mno-red-zone        \
	 -mcmodel=kernel      \
	 -fpermissive         \
	 -Wall                \
	 -Wno-write-strings   \
	 -Og                  \
	 -fno-rtti            \
	 -fno-exceptions      \
	 -fno-lto             \
	 -fno-pie             \
	 -fno-pic             \
	 -march=x86-64        \
	 -ggdb


ASMFLAGS = -f elf64

LDFLAGS = -nostdlib               \
	  -static                 \
	  -m elf_$(ARCH)          \
	  -T $(MODNAME).ld        \
	  -z max-page-size=0x1000

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

CPPSRC = $(call rwildcard,$(MODDIR),*.cpp)
ASMSRC = $(call rwildcard,$(MODDIR),*.asm)
OBJS = $(patsubst $(MODDIR)/%.cpp, $(MODDIR)/%.o, $(CPPSRC))
OBJS += $(patsubst $(MODDIR)/%.asm, $(MODDIR)/%.o, $(ASMSRC))

$(MODDIR)/%.o: $(MODDIR)/%.cpp
	@ mkdir -p $(@D)
	@ echo !==== COMPILING MODULE $^ && \
	$(CPP) $(CFLAGS) -c $^ -o $@


$(MODDIR)/%.o: $(MODDIR)/%.asm
	@ mkdir -p $(@D)
	@ echo !==== COMPILING MODULE $^  && \
	$(ASM) $(ASMFLAGS) $^ -o $@

module: $(OBJS)
	@ echo !==== LINKING
	$(LD) $(LDFLAGS) -o ../$(MODNAME).elf $(OBJS)

clean:
	@rm $(OBJS)
