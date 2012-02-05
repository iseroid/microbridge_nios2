#include <unistd.h>
#include <sys/alt_alarm.h>
#include "wiring.h"

void delay( int x )
{
	usleep( x * 1000 );
}

uint32_t millis( void )
{
	return alt_nticks();
}

