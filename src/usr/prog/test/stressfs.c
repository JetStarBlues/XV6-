// Demonstrate that moving the "acquire" in iderw after the loop that
// appends to the idequeue results in a race.

// For this to work, you should also add a spin within iderw's
// idequeue traversal loop. Adding the following demonstrated a panic
// after about 5 runs of stressfs in QEMU on a 2.1GHz CPU:
//    for ( i = 0; i < 40000; i++ )
//      asm volatile( "" );

#include "types.h"
#include "user.h"
#include "date.h"
#include "fs.h"
#include "fcntl.h"

int main ( int argc, char* argv [] )
{
	int  fd,
	     i;
	char data [ 512 ];
	char path [] = "stressfs0";

	printf( 1, "stressfs starting\n" );

	memset( data, 'a', sizeof( data ) );

	for ( i = 0; i < 4; i += 1 )
	{
		if ( fork() > 0 )
		{
			break;
		}
	}

	printf( 1, "write %d\n", i );

	path[ 8 ] += i;

	fd = open( path, O_CREATE | O_RDWR );

	for ( i = 0; i < 20; i += 1 )
	{
		// printf( fd, "%d\n", i );

		write( fd, data, sizeof( data ) );
	}

	close( fd );

	printf( 1, "read\n" );

	fd = open( path, O_RDONLY );

	for ( i = 0; i < 20; i += 1 )
	{
		read( fd, data, sizeof( data ) );
	}

	close( fd );

	wait();

	exit();
}