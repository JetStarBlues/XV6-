/*
Startup process:
	. BIOS on motherboard prepares CPU's hardware

	. The BIOS then loads the first 512 byte sector
	  from the "boot sector" into memory

	. xv6's boot sector is the first 512 byte sector
	  in "xv6.img". This corresponds to the binary
	  "bootblock" generated from "bootasm.S" and "bootmain.c".
	  See Makefile.

	. The code in bootblock sets up the CPU to run xv6 by
	  among other things:
	    . switching to 32bit mode
	    . loading the "kernel" binary into memory.
	      The "kernel" is the remaining sectors in "xv6.img"
	      (see Makefile).
	    . jumping to the "kernel" ELF's entry point, which
	      corresponds to label "_start" in "entry.S"

	. entry.S among other things:
		. turns on paging
		. sets up the (temporary) stack pointer ??
		. calls "main"

	. "main" initializes a bunch of OS things.
	  Eventually it calls "userinit" which loads the
	  "initcode" binary into the first process

	. The code in initcode calls sys_exec( fs/bin/init )

	. The code in fs/bin/init starts the shell
*/

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers ( void );
static void mpmain      ( void )  __attribute__( ( noreturn ) );

// extern pde_t* kpgdir;

extern char end [];  /* first address after kernel text and data.
                        Label is created by "kernel.ld" when creating the
                        kernel ELF */

pde_t entrypgdir [];  // Used by entry.S and entryothers.S


// Bootstrap CPU starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int main ( void )
{
	kinit1( end, P2V( 4 * 1024 * 1024 ) );  // kernel_end..4MB  ?? phys page allocator

	kvmalloc();      // create kernel page table, then switch to it ??
	mpinit();        // detect other CPUs
	lapicinit();     // interrupt controller
	seginit();       // segment descriptors
	picinit();       // disable pic
	ioapicinit();    // another interrupt controller
	consoleinit();   // console hardware
	uartinit();      // serial port
	vgainit();       // vga
	displayinit();   // generic display
	mouseinit();     // mouse
	procinit();      // process table
	trapinit();      // trap vectors
	binit();         // buffer cache
	fileinit();      // file table
	ideinit();       // disk 
	startothers();   // start other CPUs

	kinit2( P2V( 4 * 1024 * 1024 ), P2V( PHYSTOP ) );  // 4MB..PHYSTOP  ?? must come after startothers()

	userinit();      // create first user process
	mpmain();        // finish this CPU's setup

	/* The following are initialized by the first process:

	   iinit( ROOTDEV );    // ...
	   initlog( ROOTDEV );  // initialize log. Recover file system if necessary
	*/

	cprintf( "main: shouldn't reach here??\n" );
	return 0;
}

// Other CPUs jump here from entryother.S.
static void mpenter ( void )
{
	switchkvm();
	seginit();
	lapicinit();
	mpmain();
}

// Common CPU setup code.
static void mpmain ( void )
{
	cprintf( "cpu%d: starting %d\n\n", cpuid(), cpuid() );

	idtinit();  // load idt register

	xchg( &( mycpu()->started ), 1 );  // tell startothers() we're up

	scheduler();  // start running processes
}


// Start the non-boot (AP) processors.
static void startothers ( void )
{
	// extern uchar _binary_entryother_start[], _binary_entryother_size[];
	extern uchar _binary_img_entryother_start [],
	             _binary_img_entryother_size  [];  // JK, new path

	struct cpu* c;

	uchar* code;
	char*  stack;

	void ( **mpenterPtr ) ( void );  // pointer to function-pointer (function has no return value, no parameters)
	void** stackTopPtr;              // pointer to void-pointer
	int**  pgdirPtr;                 // pointer to int-pointer

	/* Write entry code to unused memory at 0x7000.
	   RAM between 0x0000_0500 and 0x0000_7BFF is guaranteed available for use.
	    https://wiki.osdev.org/Memory_Map_(x86)

	   The linker has placed the image of entryother.S in _binary_entryother_start.
	*/
	code = P2V( 0x7000 );

	memmove(

		code,
		// _binary_entryother_start,
		// ( uint ) _binary_entryother_size
		_binary_img_entryother_start,
		( uint ) _binary_img_entryother_size
	);


	// ...
	for ( c = cpus; c < cpus + ncpu; c += 1 )
	{
		if ( c == mycpu() )  // We've started already.
		{
			continue;
		}

		/* Tell entryother.S,
		     . what stack to use,
		     . what pgdir to use,
		     . where to enter kernel code after cpu startup and setup

		   We cannot use kpgdir yet, because the AP processor
		   is running in low memory, so we use entrypgdir for the APs too.
		*/
		/* TODO: What does this do??

		    0x7000 + _binary_entryother_size ->  -------------------------
		                                         contents of entryother.S
		    0x7000                           ->  -------------------------  <- start (see entryother.S)
		                                         address of stack top
		    0x7000 - 4                       ->  -------------------------  <- start - 4
		                                         address of mpenter
		    0x7000 - 8                       ->  -------------------------  <- start - 8
		                                         address of pgdir
		    0x7000 - 12                      ->  -------------------------  <- start - 12


		   See stackoverflow.com/a/57174081

		                  ( void** ) ( code - 4 )  -> pointer to pointer to void
		    ( void ( ** ) ( void ) ) ( code - 8 )  -> pointer to pointer to function (with no return value, no parameters)
		                   ( int** ) ( code - 12 ) -> pointer to pointer to int
		*/
		stack = kalloc();


		stackTopPtr =               ( void** ) ( code - 4  );
		mpenterPtr  = ( void ( ** ) ( void ) ) ( code - 8  );
		pgdirPtr    =                ( int** ) ( code - 12 );


		*stackTopPtr = stack + KSTACKSIZE;        // address of stack top

		*mpenterPtr = mpenter;                    // address of mpenter

		*pgdirPtr = ( void* ) V2P( entrypgdir );  // address of pgdir to use


		lapicstartap( c->apicid, V2P( code ) );


		// Wait for cpu to finish mpmain()
		while ( c->started == 0 )
		{
			//
		}
	}
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PDE_PS in a page directory entry enables 4Mbyte "super" pages.

/* For additional explanations see:
    . p.22 and p.37
    . stackoverflow.com/a/31171078
*/
__attribute__( ( __aligned__( PGSIZE ) ) )

pde_t entrypgdir [ NPDENTRIES ] = {

	/* Map VA's [0, 4MB] to PA's [0, 4MB]...

	   1:1 mapping

	   This mapping is used when ... 'entry' is executing at low address ?? p.22

	   (Btw, 4MB == 0x40_0000)
	*/
	[ 0 ]                        = ( 0 ) | PDE_P | PDE_W | PDE_PS,


	/* Map VA's [KERNBASE, KERNBASE + 4MB] to PA's [0, 4MB]

	   Maps high virtual address where kernel expects? to find its
	   instructions and data, to the low physical address where the
	   boot loader placed them.

	   This mapping is used when ... exit 'entry' and start executing at
	   high addresses...??

	   This mapping restricts the kernel instructions and rodata to 4 MB ??

	   (Btw, 0x8000_0000 / 4MB == 512 == KERNBASE >> 22)
	*/
	[ KERNBASE >> PD_IDX_SHIFT ] = ( 0 ) | PDE_P | PDE_W | PDE_PS,
};
