/*
  Build your own text editor
  Jeremy Ruten
  https://viewsourcecode.org/snaptoken/kilo/index.html
*/

/*

TODO:
	. 

*/


#include "types.h"
#include "user.h"
#include "GFXtext.h"  // Render graphically
#include "termios.h"

// Temp
#define stdin  0
#define stdout 1
#define stderr 2
#define NULL   0

//
#define CTRL( k ) ( ( k ) & 0x1F )  // https://en.wikipedia.org/wiki/ASCII#Control_characters


//
struct _editorState {

	struct termios origConsoleAttr;

	uint nRows;
	uint nCols;

	uint cursorRow;
	uint cursorCol;
};

static struct _editorState editorState;


//
/*struct charBuffer {

	char* buf;
	int   len;
};*/


//
void die ( char* );


// ____________________________________________________________________________________

/*void appendToBuffer ( struct charBuffer* cBuf, const char* s, int slen )
{
	char* new;

	// Grow region pointed to by cBuf.buf
	new = realloc( cBuf->buf, cBuf->len + slen );

	if ( new == NULL )
	{
		return;
	}


	// Append 's' to region
	memcpy( &( new[ cBuf->len ] ), s, slen );


	// Update
	cBuf->buf = new;
	cBuf->len += slen;
}

void freeUnderlyingBuffer ( struct charBuffer* cBuf )
{
	free( cBuf->buf );
}*/


// ____________________________________________________________________________________

void enableRawMode ( void )
{
	struct termios newConsoleAttr;

	if ( getConsoleAttr( stdin, &( editorState.origConsoleAttr ) ) < 0 )
	{
		die( "getConsoleAttr" );
	}

	// Copy editorState.origConsoleAttr to newConsoleAttr
	// memcpy( &newConsoleAttr, &( editorState.origConsoleAttr ), sizeof( struct termios ) );
	newConsoleAttr = editorState.origConsoleAttr;

	// newConsoleAttr.echo   = 0;  // disable echoing
	newConsoleAttr.icanon = 0;  // disiable canonical mode input

	if ( setConsoleAttr( stdin, &newConsoleAttr ) < 0 )
	{
		die( "setConsoleAttr" );
	}
}

void disableRawMode ( void )
{
	if ( setConsoleAttr( stdin, &( editorState.origConsoleAttr ) ) < 0 )
	{
		die( "setConsoleAttr" );
	}
}


// ____________________________________________________________________________________

/* Emulate the "Erase In Line" behaviour.

   mode 0 - erase part of line to the right of cursor (inclusive)
   mode 1 - erase part of line to the left of cursor (inclusive)
   mode 2 - erases entire line

   https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#clear-lines-one-at-a-time
*/
#define ERASELINE_RIGHT 0
#define ERASELINE_LEFT  1
#define ERASELINE_ALL   2

void eraseInLine ( uint mode )
{
	uint row;
	uint col;
	uint savedCol;

	// Invalid mode, ignore
	if ( mode > 2 )
	{
		return;
	}


	getCursorPosition( &row, &col );

	savedCol = col;  // save


	if ( mode == ERASELINE_RIGHT )
	{
		while ( col < editorState.nCols )
		{
			setCursorPosition( row, col );
			printChar( ' ' );

			col += 1;
		}
	}
	else if ( mode == ERASELINE_LEFT )
	{
		while ( col >= 0 )
		{
			setCursorPosition( row, col );
			printChar( ' ' );

			if ( col == 0 ) { break; }  // unsigned, avoid negatives...

			col -= 1;
		}
	}
	else if ( ERASELINE_ALL )
	{
		for ( col = 0; col < editorState.nCols; col += 1 )
		{
			setCursorPosition( row, col );
			printChar( ' ' );
		}
	}


	// Restore
	setCursorPosition( row, savedCol );
}

void temp_testEraseLine ( void )
{
	uint col;
	uint row;

	row = 0;

	for ( col = 0; col < editorState.nCols; col += 1 )
	{
		setCursorPosition( row, col );
		printChar( '$' );
	}

	setCursorPosition( 0, 20 );

	eraseInLine( 0 );



	row = 1;

	for ( col = 0; col < editorState.nCols; col += 1 )
	{
		setCursorPosition( row, col );
		printChar( '&' );
	}

	setCursorPosition( 1, 20 );

	eraseInLine( 1 );



	row = 2;

	for ( col = 0; col < editorState.nCols; col += 1 )
	{
		setCursorPosition( row, col );
		printChar( '#' );
	}

	setCursorPosition( 2, 20 );

	eraseInLine( 2 );
}


/* Here, since we're responsible for cursor movement...
*/
#define WRAP    0
#define NO_WRAP 1

void printString ( char* s, int wrap )
{
	uint row;
	uint col;

	getCursorPosition( &row, &col );

	while ( *s )
	{
		//
		printChar( *s );
		printf( 1, "%c", *s );


		// Advance cursor
		col += 1;

		if ( col == editorState.nCols )
		{
			if ( wrap == WRAP )
			{
				col  = 0;
				row += 1;

				// erm...
				if ( row == editorState.nRows )
				{
					row -= 1;
				}
			}
			else
			{
				break;  // erm...
			}
		}

		setCursorPosition( row, col );


		//
		s += 1;
	}
}












// ____________________________________________________________________________________

/* Key macros from 'kbd.h'

   QEMU window has to be in focus to use the associated keys.

   If the terminal/console/command-prompt that launched QEMU is
   instead in focus, you'll get a multibyte escape sequence that
   needs to be decoded. See explanation here:
    . https://viewsourcecode.org/snaptoken/kilo/03.rawInputAndOutput.html#arrow-keys 

   I think the code in 'kbd.c' does this decoding for you...
   TODO: confirm 
*/
#define KEY_HOME     0xE0  // Doesn't seem to work =(
#define KEY_END      0xE1
#define KEY_UP       0xE2
#define KEY_DOWN     0xE3
#define KEY_LEFT     0xE4
#define KEY_RIGHT    0xE5
#define KEY_PAGEUP   0xE6
#define KEY_PAGEDOWN 0xE7
#define KEY_DELETE   0xE9

//
void moveCursor ( uchar key )
{
	switch ( key )
	{
		case KEY_LEFT:

			if ( editorState.cursorCol != 0 )
			{
				editorState.cursorCol -= 1;
			}
			break;

		case KEY_RIGHT:

			if ( editorState.cursorCol < editorState.nCols - 1 )
			{
				editorState.cursorCol += 1;
			}
			break;

		case KEY_UP:

			if ( editorState.cursorRow != 0 )
			{
				editorState.cursorRow -= 1;
			}
			break;

		case KEY_DOWN:

			if ( editorState.cursorRow < editorState.nRows - 1 )
			{
				editorState.cursorRow += 1;
			}
			break;
	}
}


// ____________________________________________________________________________________

char readKey ( void )
{
	char c;

	if ( read( stdin, &c, 1 ) < 0 )
	{
		die( "read" );
	}

	printf( stdout, "(%d, %c)\n", c, c );

	return c;
}

void processKeyPress ( void )
{
	uchar c;

	c = ( uchar ) readKey();

	switch ( c )
	{
		case CTRL( 'q' ):

			die( NULL );
			break;

		case KEY_LEFT:
		case KEY_RIGHT:
		case KEY_UP:
		case KEY_DOWN:

			moveCursor( c );
			break;

		case KEY_PAGEUP:
		case KEY_PAGEDOWN:
		{
			// For now, just move to vertical extremes
			int n = editorState.nRows;
			while ( n )
			{
				moveCursor( c == KEY_PAGEUP ? KEY_UP : KEY_DOWN );
				n -= 1;
			}

			break;
		}

		case KEY_HOME:
		case KEY_END:
		{
			// For now, just move to horizontal extremes
			int n = editorState.nCols;
			while ( n )
			{
				moveCursor( c == KEY_HOME ? KEY_LEFT : KEY_RIGHT );
				n -= 1;
			}

			break;
		}

		// case KEY_DELETE:
			// break;
	}
}


// ____________________________________________________________________________________

void drawRows ( void )
{
	uint  row;
	int   center;
	char* welcomeMsg;
	int   welcomeMsgLen;

	welcomeMsg    = "Kilo Editor";
	welcomeMsgLen = 11;  // excluding null terminal

	setCursorPosition( 0, 0 );  // makes more sense here...

	for ( row = 0; row < editorState.nRows; row += 1 )
	{
		setCursorPosition( row, 0 );

		eraseInLine( ERASELINE_ALL );  // erase entire line...

		printChar( '~' );

		// Print welcome message
		if ( row == editorState.nRows / 3 )
		{
			center = ( editorState.nCols / 2 ) - ( welcomeMsgLen / 2 );

			setCursorPosition( row, center );

			printString( welcomeMsg, NO_WRAP );
		}
	}
}

void refreshScreen ( void )
{
	// clearScreen();

	// setCursorPosition( 0, 0 );
	// Does editor cursor mirror terminal's ???

	drawRows();

	// setCursorPosition( 0, 0 );
	setCursorPosition( editorState.cursorRow, editorState.cursorCol );
	drawCursor();
}


// ____________________________________________________________________________________

void initEditor ( void )
{
	editorState.cursorCol = 0;
	editorState.cursorRow = 0;

	//
	getDimensions( &editorState.nRows, &editorState.nCols );
	
}


// ____________________________________________________________________________________

void setup ( void )
{
	initGFXText();

	enableRawMode();

	initEditor();
}

void cleanup ( void )
{
	exitGFXText();

	disableRawMode();
}


void die ( char* msg )
{
	cleanup();

	if ( msg )
	{
		printf( stderr, "keditor: death by %s\n", msg );
	}

	exit();
}


// ____________________________________________________________________________________

int main ( void )
{
	setup();

	while ( 1 )
	{
		refreshScreen();

		processKeyPress();  // atm, blocks
	}

	cleanup();

	exit();
}
