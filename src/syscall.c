#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

/*
  User code makes a system call with INT T_SYSCALL.
  . System call number in %eax.
  . Arguments on the stack, from the user call to the C
    library system call function.
  . The saved user %esp points to a saved program counter,
    and then the first argument.
*/

// Fetch the int at addr from the current process.
int fetchint ( uint addr, int* ip )
{
	struct proc* curproc;

	//
	curproc = myproc();

	// Check that the address lies within the user address space
	// Why not just check (addr + 4) ??
	if ( ( addr >= curproc->sz ) || ( addr + 4 > curproc->sz ) )
	{
		return - 1;
	}

	// We can simply cast the address to a pointer because the
	// user and kernel share the same page table ??
	*ip = *( ( int* ) ( addr ) );

	return 0;
}

// Fetch the null-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including null.
int fetchstr ( uint addr, char** pp )
{
	struct proc* curproc;
	char*        s;
	char*        ep;

	//
	curproc = myproc();

	// Check that points to address within user address space
	if ( addr >= curproc->sz )
	{
		return - 1;
	}

	*pp = ( char* ) addr;

	ep = ( char* ) curproc->sz;  // Used to check that entire string lies
	                             // within user address space

	for ( s = *pp; s < ep; s += 1 )
	{
		if ( *s == 0 )  // reached null terminal
		{
			return s - *pp;
		}
	}

	return - 1;
}

// Fetch the nth 32-bit system call argument.
// Get arguments from user stack (instead of kernel stack)
int argint ( int n, int* ip )
{
	return fetchint(

		( myproc()->tf->esp ) + 4 + ( 4 * n ),  // +4 to skip the return position
		ip
	);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes. Check that the pointer
// lies within the process address space.
int argptr ( int n, char** pp, int size )
{
	struct proc* curproc;
	int          i;

	//
	curproc = myproc();
 
	if ( argint( n, &i ) < 0 )
	{
		return - 1;
	}

	// Check that points to address within user address space
	if ( size < 0                           ||
	     ( uint ) i        >= curproc->sz   ||
	     ( uint ) i + size >  curproc->sz )
	{
		return - 1;
	}

	*pp = ( char* ) i;

	return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int argstr ( int n, char** pp )
{
	int addr;

	if ( argint( n, &addr ) < 0 )
	{
		return - 1;
	}

	return fetchstr( addr, pp );
}

extern int sys_chdir   ( void );
extern int sys_close   ( void );
extern int sys_dup     ( void );
extern int sys_exec    ( void );
extern int sys_exit    ( void );
extern int sys_fork    ( void );
extern int sys_fstat   ( void );
extern int sys_getpid  ( void );
extern int sys_gettime ( void );
extern int sys_ioctl   ( void );
extern int sys_kill    ( void );
extern int sys_link    ( void );
extern int sys_mkdir   ( void );
extern int sys_mknod   ( void );
extern int sys_open    ( void );
extern int sys_pipe    ( void );
extern int sys_read    ( void );
extern int sys_sbrk    ( void );
extern int sys_sleep   ( void );
extern int sys_unlink  ( void );
extern int sys_uptime  ( void );
extern int sys_wait    ( void );
extern int sys_write   ( void );

// Array of function pointers
static int ( *syscalls[] )( void ) = {

	[ SYS_chdir   ] sys_chdir,
	[ SYS_close   ] sys_close,
	[ SYS_dup     ] sys_dup,
	[ SYS_exec    ] sys_exec,
	[ SYS_exit    ] sys_exit,
	[ SYS_fork    ] sys_fork,
	[ SYS_fstat   ] sys_fstat,
	[ SYS_getpid  ] sys_getpid,
	[ SYS_gettime ] sys_gettime,
	[ SYS_ioctl   ] sys_ioctl,
	[ SYS_kill    ] sys_kill,
	[ SYS_link    ] sys_link,
	[ SYS_mkdir   ] sys_mkdir,
	[ SYS_mknod   ] sys_mknod,
	[ SYS_open    ] sys_open,
	[ SYS_pipe    ] sys_pipe,
	[ SYS_read    ] sys_read,
	[ SYS_sbrk    ] sys_sbrk,
	[ SYS_sleep   ] sys_sleep,
	[ SYS_unlink  ] sys_unlink,
	[ SYS_uptime  ] sys_uptime,
	[ SYS_wait    ] sys_wait,
	[ SYS_write   ] sys_write,
};

void syscall ( void )
{
	struct proc* curproc;
	int          num;

	//
	curproc = myproc();

	num = curproc->tf->eax;

	// Valid syscall number
	if ( num > 0 && num < NELEM( syscalls ) && syscalls[ num ] )
	{
		// Execute the requested syscall and place its return value in tf->eax
		/* When the trap returns to user space, it will load values
		   from the trapframe into the CPU's registers.
		   Thus %eax will hold the value returned by the syscall.
		*/
		curproc->tf->eax = syscalls[ num ]();
	}
	else
	{
		cprintf(

			"%d %s: unknown syscall %d\n",
			curproc->pid,
			curproc->name,
			num
		);

		curproc->tf->eax = - 1;
	}
}
