// File-system system calls.
// Mostly argument checking (since we don't trust user code)
// and calls into file.c and fs.c.

/* With the functions that the lower layers of the file system
   provide, the implementation of most system calls is trivial.
   However a few require attention...
     . sys_link
     . sys_unlink
     . sys_open
*/

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "date.h"
#include "stat.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd ( int n, int* pfd, struct file** pf )
{
	struct file* f;
	int          fd;

	if ( argint( n, &fd ) < 0 )
	{
		return - 1;
	}

	// Check that valid file descriptor
	if ( fd < 0 || fd >= NOPENFILE_PROC )
	{
		return - 1;
	}


	f = myproc()->ofile[ fd ];

	if ( f == 0 )
	{
		return - 1;
	}


	// Return the fd
	if ( pfd )
	{
		*pfd = fd;
	}

	// Return corrsponding struct file
	if ( pf )
	{
		*pf = f;
	}

	return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success. ???
/*
   Allocates the first available file descriptor in the
   current process's list of file descriptors...
*/
static int fdalloc ( struct file* f )
{
	struct proc* curproc;
	int          fd;

	//
	curproc = myproc();

	for ( fd = 0; fd < NOPENFILE_PROC; fd += 1 )
	{
		if ( curproc->ofile[ fd ] == 0 )
		{
			curproc->ofile[ fd ] = f;

			return fd;
		}
	}

	return - 1;
}


// ___________________________________________________________________________

int sys_dup ( void )
{
	struct file* f;
	int          newfd;

	if ( argfd( 0, 0, &f ) < 0 )
	{
		return - 1;
	}

	newfd = fdalloc( f );  // in addition to existing...

	if ( newfd < 0 )
	{
		return - 1;
	}

	filedup( f );  // increment ref count of file f

	return newfd;
}

int sys_read ( void )
{
	struct file* f;
	char*        p;
	int          n;

	if ( argfd( 0, 0, &f )  < 0   ||
		 argint( 2, &n )    < 0   ||
		 argptr( 1, &p, n ) < 0 )
	{
		return - 1;
	}

	return fileread( f, p, n );
}

int sys_write ( void )
{
	struct file* f;
	char*        p;
	int          n;

	if ( argfd( 0, 0, &f )  < 0   ||
		 argint( 2, &n )    < 0   ||
		 argptr( 1, &p, n ) < 0 )
	{
		return - 1;
	}

	return filewrite( f, p, n );
}

int sys_close ( void )
{
	struct file* f;
	int          fd;

	if ( argfd( 0, &fd, &f ) < 0 )
	{
		return - 1;
	}

	myproc()->ofile[ fd ] = 0;

	fileclose( f );

	return 0;
}

int sys_fstat ( void )
{
	struct file* f;
	struct stat* st;

	if ( argfd( 0, 0, &f ) < 0 ||
		 argptr( 1, ( void* ) &st, sizeof( *st ) ) < 0 )
	{
		return - 1;
	}

	return filestat( f, st );
}


// ___________________________________________________________________________

// Create the path 'newpath' as a link to the same inode as path 'oldpath'
int sys_link ( void )
{
	struct inode* dir;
	struct inode* ip;
	char          name [ FILENAMESZ ];
	char*         newpath;
	char*         oldpath;

	// Fetch arguments
	if ( argstr( 0, &oldpath ) < 0 || argstr( 1, &newpath ) < 0 )
	{
		return - 1;
	}

	begin_op();

	ip = namei( oldpath );

	if ( ip == 0 )
	{
		end_op();

		return - 1;  // oldpath does not exist
	}

	ilock( ip );

	if ( ip->type == T_DIR )
	{
		iunlockput( ip );

		end_op();

		return - 1;  // oldpath is a directory
	}

	// Increment the inode's link count
	ip->nlink += 1;

	iupdate( ip );  // write changes to disk

	iunlock( ip );


	// Create a new directory entry for it in directory newpath
	dir = nameiparent( newpath, name );

	if ( dir == 0 )
	{
		goto bad;  // newpath does not exist
	}

	ilock( dir );

	if ( dir->dev != ip->dev || dirlink( dir, name, ip->inum ) < 0 )
	{
		/* a) newpath is not on the same device (an error since
		      inode numbers only have a unique meaning on a single disk)
		   b) dirlink failed
		*/
		iunlockput( dir );

		goto bad;
	}

	iunlockput( dir );

	iput( ip );

	end_op();

	return 0;

bad:

	ilock( ip );

	ip->nlink -= 1;  // decrement (undo increment)

	iupdate( ip );

	iunlockput( ip );

	end_op();

	return - 1;
}

// Is the directory empty except for "." and ".." ?
static int isdirempty ( struct inode* dir )
{
	struct dirent direntry;
	int           off;
	int           nread;

	for ( off = 2 * sizeof( direntry ); off < dir->size; off += sizeof( direntry ) )
	{
		nread = readi( dir, ( char* ) &direntry, off, sizeof( direntry ) );

		if ( nread != sizeof( direntry ) )
		{
			panic( "isdirempty: readi" );
		}

		if ( direntry.inum != 0 )
		{
			return 0;
		}
	}

	return 1;
}

// ...
int sys_unlink ( void )
{
	struct inode*  ip;
	struct inode*  parentdir;
	struct dirent  direntry;
	char           name [ FILENAMESZ ];
	char*          path;
	uint           off;
	int            nbytes;

	if ( argstr( 0, &path ) < 0 )
	{
		return - 1;
	}

	begin_op();

	parentdir = nameiparent( path, name );

	if ( parentdir == 0 )
	{
		end_op();

		return - 1;
	}

	ilock( parentdir );

	// Cannot unlink "." or "..".
	if ( namecmp( name, "." ) == 0 || namecmp( name, ".." ) == 0 )
	{
		goto bad;
	}


	// Get inode associated with 'name'
	ip = dirlookup( parentdir, name, &off );

	if ( ip == 0 )
	{
		goto bad;
	}

	ilock( ip );

	if ( ip->nlink < 1 )
	{
		panic( "unlink: nlink < 1" );
	}

	if ( ip->type == T_DIR && ! isdirempty( ip ) )
	{
		iunlockput( ip );

		goto bad;
	}


	// Clear the directory entry
	memset( &direntry, 0, sizeof( direntry ) );

	nbytes = writei( parentdir, ( char* ) &direntry, off, sizeof( direntry ) );

	if ( nbytes != sizeof( direntry ) )
	{
		panic( "unlink: writei" );
	}

	if ( ip->type == T_DIR )
	{
		parentdir->nlink -= 1;

		iupdate( parentdir );
	}

	iunlockput( parentdir );


	// Update the inode's link count
	ip->nlink -= 1;

	iupdate( ip );

	iunlockput( ip );


	end_op();

	return 0;

bad:

	iunlockput( parentdir );

	end_op();

	return - 1;
}


// ___________________________________________________________________________

/* Whereas sys_link creates a new name for an existing inode,
   'create' creates a new name for a new inode...

   create is a generalization of three file creating syscalls:
 
     . open with the O_CREATE flag
       . makes an ordinary file

     . mkdir
       . makes a new directory

     . mkdev?/mknod?
       . makes a new device file
*/
static struct inode* create ( char* path, short type, short major, short minor )
{
	struct inode* ip;
	struct inode* parentdir;
	char          name [ FILENAMESZ ];

	// Get parent directory
	parentdir = nameiparent( path, name );

	if ( parentdir == 0 )
	{
		return 0;
	}

	ilock( parentdir );


	// Check if name is already present in parent directory
	ip = dirlookup( parentdir, name, 0 );

	if ( ip != 0 )
	{
		iunlockput( parentdir );

		ilock( ip );

		// If it's a file and we wanted a file, return it
		if ( type == T_FILE && ip->type == T_FILE )
		{
			return ip;
		}

		iunlockput( ip );

		// Otherwise fail
		return 0;
	}


	// Does not exist, so let's create it

	// Allocate an inode
	ip = ialloc( parentdir->dev, type );

	if ( ip == 0 )
	{
		panic( "create: ialloc" );
	}

	ilock( ip );


	// Initialize the inode
	ip->major = major;
	ip->minor = minor;
	ip->nlink = 1;

	cmostime( &( ip->mtime ) );  // set current time as mtime

	iupdate( ip );


	// If we are creating a directory, add the . and .. entries
	if ( type == T_DIR )
	{
		parentdir->nlink += 1;   // for ".."

		iupdate( parentdir );


		// No ip->nlink += 1 for ".": avoid cyclic ref count.

		if ( dirlink( ip,  ".", ip->inum        ) < 0  ||  // current directory
			 dirlink( ip, "..", parentdir->inum ) < 0 )    // parent directory
		{
			panic( "create: dots" );
		}
	}


	// Add the new file to the parent directory
	if ( dirlink( parentdir, name, ip->inum ) < 0 )
	{
		panic( "create: dirlink" );
	}

	iunlockput( parentdir );

	return ip;
}


// ___________________________________________________________________________

/*
*/
int sys_open ( void )
{
	struct inode* ip;
	struct file*  f;
	char*         path;
	int           fd,
	              omode;
	uint          foffset;  // JK

	if ( argstr( 0, &path ) < 0 || argint( 1, &omode ) < 0 )
	{
		return - 1;
	}


	begin_op();

	// Create if doesn't already exist
	if ( omode & O_CREATE )
	{
		ip = create( path, T_FILE, 0, 0 );

		if ( ip == 0 )
		{
			cprintf( "sys_open: create '%s' failed\n", path );

			end_op();

			return - 1;
		}
	}
	// Lookup existing
	else
	{
		// If doesn't exist, fail
		ip = namei( path );

		if ( ip == 0 )
		{
			// cprintf( "sys_open: file '%s' not found\n", path );

			end_op();

			return - 1;
		}

		ilock( ip );  // unlike 'create', 'namei' does not return a locked inode
	}


	/* By default, 'foffset' points to start of file.
	   stackoverflow.com/a/59886657
	*/
	foffset = 0;

	// If opened for writing, place 'foffset' based on flags
	if ( ( omode & O_WRONLY ) || ( omode & O_RDWR ) )
	{
		if ( ip->type == T_FILE )
		{
			/* If O_TRUNC, set file size to zero
			   (and free previously allocated data blocks)
			*/
			if ( omode & O_TRUNC )
			{
				itrunc( ip );
			}

			// If O_APPEND, set file offset to EOF
			else if ( omode & O_APPEND )
			{
				foffset = ip->size;
			}
		}

		// Hmm...
		else if ( ip->type == T_DEV )
		{
			if ( omode & O_TRUNC )
			{
				cprintf( "sys_open: T_DEV cannot be opened with O_TRUNC\n" );

				goto bad;
			}

			/* TODO: O_APPEND seems to be persistent flag that follows 'struct file'.
			         Then each device, can choose whether to handle or ignore...
			          https://stackoverflow.com/q/20268627
			*/
			else if ( omode & O_APPEND )
			{
				foffset = ip->size;
			}
		}

		// Can only read directories
		else if ( ip->type == T_DIR )
		{
			cprintf( "sys_open: T_DIR cannot be opened with O_WRONLY or O_RDWR\n" );

			goto bad;
		}
	}


	// Allocate a file structure and file descriptor
	f = filealloc();

	fd = fdalloc( f );

	if ( ( f == 0 ) || ( fd < 0 ) )
	{
		if ( f )
		{
			fileclose( f );
		}

		goto bad;
	}

	iunlock( ip );

	end_op();

	//
	f->type     = FD_INODE;
	f->ip       = ip;
	f->offset   = foffset;
	f->readable = ! ( omode & O_WRONLY );
	f->writable = ( omode & O_WRONLY ) || ( omode & O_RDWR );

	return fd;

bad:

	iunlockput( ip );

	end_op();

	return - 1;
}


// ___________________________________________________________________________

int sys_mknod ( void )
{
	struct inode* ip;
	char*         path;
	int           major,
	              minor;

	// Get args
	if ( argstr( 0, &path )  < 0   ||
		 argint( 1, &major ) < 0   ||
		 argint( 2, &minor ) < 0 )
	{
		return - 1;
	}


	//
	begin_op();

	ip = create( path, T_DEV, major, minor );

	if ( ip == 0 )
	{
		end_op();

		return - 1;
	}

	iunlockput( ip );

	end_op();

	return 0;
}

int sys_mkdir ( void )
{
	struct inode* ip;
	char*         path;

	// Get args
	if ( argstr( 0, &path ) < 0 )
	{
		return - 1;
	}


	//
	begin_op();

	ip = create( path, T_DIR, 0, 0 );

	if ( ip == 0 )
	{
		end_op();

		return - 1;
	}

	iunlockput( ip );

	end_op();

	return 0;
}


// ___________________________________________________________________________

int sys_chdir ( void )
{
	char*         path;
	struct inode* ip;
	struct proc*  curproc;

	curproc = myproc();

	// Get args
	if ( argstr( 0, &path ) < 0 )
	{
		return - 1;
	}

	
	//
	begin_op();

	ip = namei( path );

	if ( ip == 0 )
	{
		end_op();

		return - 1;
	}


	ilock( ip );

	if ( ip->type != T_DIR )
	{
		iunlockput( ip );

		end_op();

		return - 1;
	}

	iunlock( ip );


	iput( curproc->cwd );

	end_op();

	curproc->cwd = ip;

	return 0;
}


// ___________________________________________________________________________

int sys_exec ( void )
{
	char* path;
	char* argv [ MAXARG ];
	int   i;
	uint  uargv;
	uint  uarg;

	//
	if ( argstr( 0, &path )           < 0   ||  // path to executable
	     argint( 1, ( int* ) &uargv ) < 0 )     // arguments to provide execuatable
	{
		return - 1;
	}


	//
	memset( argv, 0, sizeof( argv ) );

	for ( i = 0; ; i += 1 )
	{
		if ( i >= NELEM( argv ) )
		{
			return - 1;
		}

		// Get pointer to string argument
		if ( fetchint( uargv + ( 4 * i ), ( int* ) &uarg ) < 0 )
		{
			return - 1;
		}

		// Reached last argument
		if ( uarg == 0 )
		{
			argv[ i ] = 0;

			break;
		}

		// Get the string
		if ( fetchstr( uarg, &argv[ i ] ) < 0 )
		{
			return - 1;
		}
	}

	return exec( path, argv );
}


// ___________________________________________________________________________

/* Connects pipe to file system by providing a way to
   create a pipe pair...
   Its argument is a pointer to space for two integers, where
   it will record the two new file descriptors...
   It allocates the pipe and installs the file descriptors...
*/
int sys_pipe ( void )
{
	struct file* rf;
	struct file* wf;
	int*         fdArray;  // array of two integers
	int          fd0,
	             fd1;

	// Get fdArray arg... "pipe( fdArray )"
	if ( argptr( 0, ( void* ) &fdArray, 2 * sizeof( int ) ) < 0 )
	{
		return - 1;
	}

	/* Create a pipe.
	   Also allocate file structures (rf and wf) and set them as
	   the pipe's read and write ends...
	*/
	if ( pipealloc( &rf, &wf ) < 0 )
	{
		return - 1;
	}


	/* Allocate file descriptors (in the current process) for the
	   pipe's read and write ends
	*/
	fd0 = fdalloc( rf );

	if ( fd0 < 0 )
	{
		fileclose( rf );

		fileclose( wf );

		return - 1;
	}

	fd1 = fdalloc( wf );

	if ( fd1 < 0 )
	{
		// fd0 was successfully allocated, deallocate it
		myproc()->ofile[ fd0 ] = 0;

		fileclose( rf );

		fileclose( wf );

		return - 1;
	}


	/* Return the pipe's file descriptors to the caller.
	   Indirectly return by updating contents pointed to by arg 'fd'.
	*/
	fdArray[ 0 ] = fd0;
	fdArray[ 1 ] = fd1;

	return 0;
}


// ___________________________________________________________________________

/* int ioctl ( int fd, int request, ... );

   https://github.com/DoctorWkt/xv6-freebsd/blob/master/kern/sysfile.c
*/
int sys_ioctl ( void )
{
	struct file* f;
	int          request;

	if ( argfd( 0, 0, &f ) < 0      ||
		 argint( 1, &request ) < 0 )
	{
		return - 1;
	}

	if ( ( f->type != FD_INODE ) || ( f->ip->type != T_DEV ) )
	{
		return - 1;
	}

	if ( f->ip->major < 0 || f->ip->major >= NDEV || ! devsw[ f->ip->major ].ioctl )
	{
		return - 1;
	}

	return devsw[ f->ip->major ].ioctl( f->ip, request );
}


// ___________________________________________________________________________

/* int lseek ( int fd, uint offset, int whence );

   https://github.com/DoctorWkt/xv6-freebsd/blob/master/kern/sysfile.c

   TODO: Write a proper test, and check works as expected

   TODO: O_APPEND is persistent. If file opened with this flag,
         position set by lseek is ignored, and all writes occur at EOF.
           https://stackoverflow.com/a/59886657
           https://stackoverflow.com/a/24223731
*/
int sys_lseek ( void )
{
	struct file* f;
	int          whence;
	uint         offset;
	uint         newOffset;

	char*        zeros;
	int          zeroSize;
	int          i;
	int          nWritten;
	char*        ptr;


	// Get args from user stack
	if ( argfd( 0, 0, &f ) < 0                 ||
		 argint( 1, ( int* ) ( &offset ) ) < 0 ||
		 argint( 1, &whence ) < 0 )
	{
		return - 1;
	}


	// Check that not seeking a pipe
	if ( f->type == FD_PIPE )
	{
		return - 1;
	}


	// New offset is 'offset'
	if ( whence == SEEK_SET )
	{
		newOffset = offset;
	}
	// New offset is current location plus 'offset'
	else if ( whence == SEEK_CUR )
	{
		newOffset = f->offset + offset;
	}
	// New offset is size of file plus 'offset'
	else if ( whence == SEEK_END )
	{
		newOffset = f->ip->size + offset;
	}
	// Unknown specifier
	else
	{
		return - 1;
	}


    /* From man pages:
        "lseek() allows the file offset to be set beyond the end of the file
         (but this does not change the size of the file). If data is later
         written at this point, subsequent reads of the data in the gap (a
         "hole") return null bytes ('\0') until data is actually written into
         the gap."
    */
	/* JK - the code below seems to append zeros to the end of the file,
	   and thus change the file size...
	*/
	if ( newOffset > f->ip->size )
	{
		zeroSize = ( int ) ( newOffset - f->ip->size );

		zeros = kalloc();


		// Zero block returned by kalloc...
		ptr = zeros;

		for ( i = 0; i < PGSIZE; i += 1 )
		{
			*ptr = 0;

			ptr += 1;
		}


		// Append zeros to the end of the file...
		f->offset = f->ip->size;  // JK, added because shouldn't we be appending to end ??

		while ( zeroSize > 0 )
		{
			nWritten = filewrite( f, zeros, MIN( PGSIZE, zeroSize ) );

			if ( nWritten < 0 )
			{
				return - 1;
			}

			zeroSize -= nWritten;
		}


		//
		kfree( zeros );
	}


	// Update
	f->offset = newOffset;

	return newOffset;
}
