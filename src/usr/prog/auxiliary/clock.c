// https://youtu.be/2rAnCmXaOwo

#include "kernel/types.h"
#include "kernel/date.h"
#include "user.h"
#include "time.h"

int main ( int argc, char* argv [] )
{
	struct rtcdate d;

	gettime( &d );

	printf( stdout, "The time is:\n\n" );

	printf( stdout,

		"  %2d:%02d:%02d\n"
		"  %s, %s %d, %d\n",

		d.hour, d.minute, d.second,
		stringify_weekday( d.weekday ),
		stringify_month( d.month ),
		d.monthday,
		d.year
	);

	printf( stdout, "\n" );

	printf( stdout,

		"rtcdate:\n"
		"  second   - %d\n"
		"  minute   - %d\n"
		"  hour     - %d\n"
		"  weekday  - %d\n"
		"  monthday - %d\n"
		"  month    - %d\n"
		"  year     - %d\n",

		d.second,
		d.minute,
		d.hour,
		d.weekday,
		d.monthday,
		d.month,
		d.year
	);

	exit();
}
