#include "segasm.h"
#include "memlayout.h"
#include "mmu.h"

# Start the first CPU: switch to 32-bit protected mode, jump into C.
# The BIOS loads this code from the first sector of the hard disk into
# memory at physical address 0x7C00 and starts executing in real mode
# with %cs=0 %ip=0x7C00.

.code16  # Assemble for 16-bit mode

.globl start  # 0x7C00
start:

	# Disable interrupts on main CPU. (BIOS previously enabled interrutps)
	cli

	# Zero data segment registers DS, ES, and SS.
	xorw   %ax, %ax   # Set %ax to zero
	movw   %ax, %ds   # -> Data Segment
	movw   %ax, %es   # -> Extra Segment
	movw   %ax, %ss   # -> Stack Segment


# Compatability hack:
# Physical address line A20 is tied to zero so that the first PCs 
# with 2 MB would run software that assumed 1 MB. Undo that.
# https://www.win.tue.nl/~aeb/linux/kbd/A20.html
seta20.1:

	inb    $0x64, %al    # Wait for not busy
	testb  $0x2,  %al
	jnz    seta20.1

	movb   $0xd1, %al    # 0xd1 -> port 0x64
	outb   %al,   $0x64  # send command to keyboard

seta20.2:

	inb    $0x64, %al    # Wait for not busy
	testb  $0x2,  %al
	jnz    seta20.2

	movb   $0xdf, %al    # 0xdf -> port 0x60
	outb   %al,   $0x60  # write to keyboard input buffer...


	# Switch from real to protected mode. Use a bootstrap GDT that makes
	# virtual addresses map directly to physical addresses so that the
	# effective memory map doesn't change during the transition.
	lgdt   gdtdesc
	movl   %cr0,    %eax
	orl    $CR0_PE, %eax
	movl   %eax,    %cr0

	# Complete the transition to 32-bit protected mode by using a long jmp
	# to reload %cs and %eip. The segment descriptors are set up with no
	# translation, so that the mapping is still the identity mapping.
	ljmp   $( SEG_KCODE << 3 ), $start32


.code32  # Tell assembler to generate 32-bit code now.

start32:

	# Set up the protected-mode data segment registers
	movw    $( SEG_KDATA << 3 ), %ax  # Our data segment selector
	movw    %ax, %ds                  # -> DS: Data Segment
	movw    %ax, %es                  # -> ES: Extra Segment
	movw    %ax, %ss                  # -> SS: Stack Segment
	movw    $0,  %ax                  # Zero segments not ready for use
	movw    %ax, %fs                  # -> FS
	movw    %ax, %gs                  # -> GS

	# Set up the stack pointer
	# %esp is set to 0x7C00. This is not a problem since
	# the stack will grow downwards towards 0
	movl    $start, %esp

	# Call into the C portion of the bootblock
	call    bootmain


	# If bootmain returns (it shouldn't), trigger a Bochs
	# breakpoint if running under Bochs, then loop.
	movw    $0x8a00, %ax  # 0x8a00 -> port 0x8a00
	movw    %ax, %dx
	outw    %ax, %dx
	movw    $0x8ae0, %ax  # 0x8ae0 -> port 0x8a00
	outw    %ax, %dx

# Before spinning, a real bootloader might try to print
# an error message first.
spin:

	jmp     spin


# Bootstrap GDT
.p2align 2  # force 4 byte alignment

gdt:

	SEG_NULLASM                              # null seg
	SEG_ASM( STA_X | STA_R, 0, 0xffffffff )  # code seg
	SEG_ASM( STA_W,         0, 0xffffffff )  # data seg

gdtdesc:

	.word  ( gdtdesc - gdt - 1 )  # sizeof( gdt ) - 1
	.long  gdt                    # address gdt
