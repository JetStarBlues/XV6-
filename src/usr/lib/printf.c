#include "types.h"
#include "user.h"

/* Format specification retrieved from:
     The C Programming Language, Kernighan & Ritchie,
     2nd ed. Appendix B 1.2

   Also referenced the following code (for va_arg variants):
     . https://github.com/SerenityOS/serenity/blob/master/Libraries/LibC/stdio.cpp
     . https://github.com/SerenityOS/serenity/blob/master/AK/PrintfImplementation.h
*/

/* Currently understands:

	. the flags:

		'-' - pad with trailing whitespace.
		      Default is to pad with leading whitespace.

		'0' - pad with leading zeros

	. the conversion characters:

		'd' - print as signed integer with decimal notation
		'x' - print as unsigned integer with hexadecimal notation
		'p' - treated the same as 'x'
		'c' - print as unsigned character with ASCII notation
		's' - print as ASCII string
*/

/* TODO:

	- snprintf
*/


// _____________________________________________________________________________

#define MAXNDIGITS 16

static char* flags       = "-0";
static char* conversions = "dxpcs";

// Pass this around to track progress, (in leiu of globals)
struct printState {

	char* dstString;  // sprintf variants
	int   dstFd;      //

	int nWritten;     // bytes written thus far
	int nToWrite;     // max bytes to write for snprintf variants
};


// _____________________________________________________________________________

static void fd_putc ( int c, struct printState* pState )
{
	write( pState->dstFd, &c, 1 );

	pState->nWritten += 1;
}

static void str_putc ( int c, struct printState* pState )
{
	*( pState->dstString ) = c;

	pState->dstString += 1;

	pState->nWritten += 1;
}

static void strn_putc ( int c, struct printState* pState )
{
	if ( pState->nWritten < pState->nToWrite )
	{
		*( pState->dstString ) = c;

		pState->dstString += 1;

		pState->nWritten += 1;
	}
}


// _____________________________________________________________________________

// int printf   ( int, const char*, ... );
// int sprintf  ( char*, const char*, ... );
// int snprintf ( char*, int, const char*, ... );

// int vprintf   ( int, const char*, va_list );
// int vsprintf  ( char*, const char*, va_list );
// int vsnprintf ( char*, int, const char*, va_list );


int _printf ( struct printState*, void ( * ) ( int ), const char*, va_list );


int printf ( int fd, const char* fmt, ... )
{
	struct printState pState;
	va_list           argp;

	//
	pState.dstFd = fd;
// pState.nWritten = 0;


	//
	va_start( argp, fmt );

	_printf( &pState, fd_putc, fmt, argp );

	va_end( argp );
}

int sprintf ( char* str, const char* fmt, ... )
{
	struct printState pState;
	va_list           argp;

	//
	pState.dstString = str;


	//
	va_start( argp, fmt );

	_printf( &pState, str_putc, fmt, argp );

	va_end( argp );
}

int snprintf ( char* str, int size, const char* fmt, ... )
{
	struct printState pState;
	va_list           argp;

	//
	pState.dstString = str;
	pState.nToWrite  = size;


	//
	va_start( argp, fmt );

	_printf( &pState, strn_putc, fmt, argp );

	va_end( argp );
}



int vprintf ( int fd, const char* fmt, va_list argp )
{
	struct printState pState;

	//
	pState.dstFd = fd;

	//
	_printf( &pState, fd_putc, fmt, argp );
}

int vsprintf ( char* str, const char* fmt, va_list argp )
{
	struct printState pState;

	//
	pState.dstString = str;

	//
	_printf( &pState, str_putc, fmt, argp );
}

int vsnprintf ( char* str, int size, const char* fmt, va_list argp )
{
	struct printState pState;

	//
	pState.dstString = str;
	pState.nToWrite  = size;

	//
	_printf( &pState, strn_putc, fmt, argp );
}


// _____________________________________________________________________________

/* For itoa explanation, see:
     The C Programming Language, Kernighan & Ritchie,
     2nd ed. Section 3.6
*/
/* Returns string version of int 'xx' in 'sint',
   and sign in 'sign'
*/
static void itoa ( int xx, int base, int issigned, char* sign, char sint [] )
{
	static char digits [] = "0123456789ABCDEF";

	char buf [ MAXNDIGITS + 1 ];

	uint x;
	int  i;
	int  j;
	int  isneg;


	// Determine sign
	isneg = 0;

	if ( issigned && ( xx < 0 ) )
	{
		isneg = 1;

		x = - xx;  // abs value
	}
	else
	{
		x = xx;
	}


	// itoa
	i = 0;
	do
	{
		buf[ i ] = digits[ x % base ];

		x /= base;

		i += 1;
	}
	while ( ( x > 0 ) && ( i < MAXNDIGITS ) );


	/* Return integer string.
	   Digits are reversed, so print back to front
	*/
	i -= 1;

	j = 0;

	while ( i >= 0 )
	{
		sint[ j ] = buf[ i ];

		j += 1;

		i -= 1;
	}

	sint[ j ] = 0;  // null terminate


	// Return sign
	if ( issigned )
	{
		if ( isneg )
		{
			*sign = '-';
		}
		else
		{
			*sign = '+';
		}
	}
	else
	{
		*sign = 0;
	}
}


// _____________________________________________________________________________

static void printPadding ( struct printState* pState, void ( *putc ) ( int ), int n, int padWithZero )
{
	while ( n )
	{
		if ( padWithZero )
		{
			putc( '0', pState );
		}
		else
		{
			putc( ' ', pState );
		}

		n -= 1;
	}
}

static void print_d ( struct printState* pState, void ( *putc ) ( int ), va_list argp, int width, int padLeft, int padRight, int padWithZero )
{
	// uint* argp;
	char  sint [ MAXNDIGITS + 1 ];
	char  sign;
	int   i;
	int   x;
	int   npad;
	int   printSign;

	// Get integer from argp
	/*argp = *argpp;      // create a local copy of argp

	x = ( int ) *argp;  //

	*argpp += 1;        // increment original argp
	*/
	x = va_arg( argp, int );


	// Convert integer to string
	itoa(

		x,
		10,     // base 10
		1,      // signed
		&sign,
		sint
	);


	/* For now, always print sign for negative.
	   In the future, can be parameter (based on flags).
	*/
	printSign = x < 0;


	// Calculate padding
	npad = width - strlen( sint );

	if ( printSign )
	{
		npad -= 1;
	}

	if ( npad < 0 )
	{
		npad = 0;
	}


	/* Print leading characters
	*/
	// If padding with zeros, sign comes first
	if ( padWithZero )
	{
		if ( printSign )
		{
			putc( sign, pState );
		}

		printPadding( pState, putc, npad, 1 );
	}
	// If padding with spaces, padding comes first
	else
	{
		printPadding( pState, putc, npad, 0 );

		if ( printSign )
		{
			putc( sign, pState );
		}
	}

	// Print integer string
	i = 0;

	while ( sint[ i ] )
	{
		putc( sint[ i ], pState );

		i += 1;
	}

	// Print trailing spaces
	if ( padRight )
	{
		printPadding( pState, putc, npad, 0 );
	}
}

static void print_x ( struct printState* pState, void ( *putc ) ( int ), va_list argp, int width, int padLeft, int padRight, int padWithZero )
{
	// uint* argp;
	char  sint [ MAXNDIGITS + 1 ];
	char  sign;
	int   i;
	int   x;
	int   npad;

	// Get integer from argp
	/*argp = *argpp;      // create a local copy of argp

	x = ( int ) *argp;  //

	*argpp += 1;        // increment original argp
	*/
	x = va_arg( argp, int );


	// Convert integer to string
	itoa(

		x,
		16,     // base 16
		0,      // unsigned
		&sign,
		sint
	);


	// Calculate padding
	npad = width - strlen( sint );

	if ( npad < 0 )
	{
		npad = 0;
	}


	// Print leading spaces or zeros
	if ( padLeft )
	{
		printPadding( pState, putc, npad, padWithZero );
	}

	// Print integer string
	i = 0;

	while ( sint[ i ] )
	{
		putc( sint[ i ], pState );

		i += 1;
	}

	// Print trailing spaces
	if ( padRight )
	{
		printPadding( pState, putc, npad, 0 );
	}
}

static void print_c ( struct printState* pState, void ( *putc ) ( int ), va_list argp, int width, int padLeft, int padRight )
{
	// uint* argp;
	char  c;
	int   npad;

	// Get char from argp
	/*argp = *argpp;       // create a local copy of argp

	c = ( char ) *argp;  //

	*argpp += 1;         // increment original argp*/
	                     /* It seems that chars pushed as int to stack...
	                     */

	/* It seems that chars pushed as int to stack...

	    stackoverflow.com/a/28308615
	    stackoverflow.com/a/28054417

	    "For a variadic function, the compiler doesn't know the types
	     of the parameters corresponding to the ', ...'
	     For historical reasons, and to make the compiler's job easier,
	     any corresponding arguments of types narrower than int
	     are promoted to int or to unsigned int, and any arguments of type
	     float are promoted to double."
	*/
	c = va_arg( argp, int );  /* 
	                              */


	// Calculate padding
	npad = width - 1;

	if ( npad < 0 )
	{
		npad = 0;
	}


	// Print leading spaces
	if ( padLeft )
	{
		printPadding( pState, putc, npad, 0 );
	}

	// Print char
	putc( c, pState );

	// Print trailing spaces
	if ( padRight )
	{
		printPadding( pState, putc, npad, 0 );
	}
}

static void print_s ( struct printState* pState, void ( *putc ) ( int ), va_list argp, int width, int padLeft, int padRight )
{
	// uint* argp;
	char* s;
	int   npad;

	// Get string from argp
	/*argp = *argpp;        // create a local copy of argp

	s = ( char* ) *argp;  //

	*argpp += 1;          // increment original argp*/
	                      /* Pointer, sizeof( char* ), seems to be sizeof( int )
	                      */
	s = va_arg( argp, char* );


	// Why so nice?
	if ( s == 0 )
	{
		s = "(null)";
	}


	// Calculate padding
	npad = width - strlen( s );

	if ( npad < 0 )
	{
		npad = 0;
	}


	// Print leading spaces
	if ( padLeft )
	{
		printPadding( pState, putc, npad, 0 );
	}

	// Print string
	while ( *s != 0 )
	{
		putc( *s, pState );

		s += 1;
	}

	// Print trailing spaces
	if ( padRight )
	{
		printPadding( pState, putc, npad, 0 );
	}
}


// _____________________________________________________________________________

int _printf ( struct printState* pState, void ( *putc ) ( int ), const char* fmt, va_list argp );
{
	char  c;
	int   i;

	int flag_padTrailing;
	int flag_padWithZero;

	int  width;
	int  swidthIdx;
	char swidth [ MAXNDIGITS + 1 ];

	// int  precision;
	// int  sprecisionIdx;
	// char sprecision [ MAXNDIGITS + 1 ];

	int padLeft;
	int padRight;


	// Parse format
	for ( i = 0; fmt[ i ]; i += 1 )
	{
		//
		c = fmt[ i ];


		/* The format string contains two types of objects:
		     . ordinary characters
		     . conversion specifications
		   A '%' delineates the latter
		*/

		/* If ordinary character, print it as is
		*/
		if ( c != '%' )
		{
			putc( c, pState );

			continue;
		}		

		/* Otherwise, a conversion has been specified.
		   We will interpret it and print accordingly
		*/

		// Skip the '%'
		i += 1;

		c = fmt[ i ];


		// __ Step 0: Initialize conversion variables ____________

		flag_padTrailing = 0;
		flag_padWithZero = 0;

		width       = 0;
		swidthIdx   = 0;
		swidth[ 0 ] = 0;

		// precision       = 0;
		// sprecisionIdx   = 0;
		// sprecision[ 0 ] = 0;

		padLeft  = 0;
		padRight = 0;


		// __ Step 1: Gather flags _______________________________

		while ( strchr( flags, c ) )
		{
			if ( c == '-' )
			{
				flag_padTrailing = 1;
			}
			else if ( c == '0' )
			{
				flag_padWithZero = 1;
			}

			// Get next character in fmt
			i += 1;

			c = fmt[ i ];
		}


		// __ Step 2: Gather width _______________________________

		if ( ISDIGIT( c ) )
		{
			// Gather number
			while ( ISDIGIT( c ) )
			{
				//
				if ( swidthIdx >= MAXNDIGITS )
				{
					/* Since we can't printf error, we instead
					   silently truncate =(
					*/ 
					break;
				}

				// 
				swidth[ swidthIdx ] = c;

				swidthIdx += 1;


				// Get next character in fmt
				i += 1;

				c = fmt[ i ];
			}

			swidth[ swidthIdx ] = 0;  // null terminate

			width = atoi( swidth );
		}


		// __ Step 3: Gather precision ___________________________

		// TODO


		// __ Step 4: Print formatted value ______________________

		// If valid conversion
		if ( strchr( conversions, c ) )
		{
			// Determine padding
			padLeft  = width && ( ! flag_padTrailing );
			padRight = width &&     flag_padTrailing;


			// Print conversion
			if ( c == 'd' )
			{
				print_d( pState, putc, argp, width, padLeft, padRight, flag_padWithZero );
			}
			else if ( c == 'x' || c == 'p' )
			{
				print_x( pState, putc, argp, width, padLeft, padRight, flag_padWithZero );
			}
			else if ( c == 'c' )
			{
				print_c( pState, putc, argp, width, padLeft, padRight );
			}
			else if ( c == 's' )
			{
				print_s( pState, putc, argp, width, padLeft, padRight );
			}
		}

		// An escaped '%'
		else if ( c == '%' )
		{
			putc( c, pState );
		}

		// Unknown % sequence. Print it to draw attention.
		else
		{
			putc( '%', pState );
			putc( c,   pState );
		}
	}
}
