#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_stdio.h"
#include "wiring.h"
#include "adb.h"
#include <stdio.h>

// Adb connection.
static Connection *connection;

static long lastTime;

static uint16_t count;

static uint8_t led;

// Event handler for the shell connection. 
static void adbEventHandler(Connection * connection, adb_eventType event, uint16_t length, uint8_t * data)
{
	// Data packets contain two bytes, one for each servo, in the range of [0..180]
	if( event == ADB_CONNECTION_RECEIVE ) {
//		alt_printf( "Rx %x\n", data[0] );
		led = (led & 0xF0) | ((data[0] >> 2) & 0x0F);
	}
	else if( event == ADB_CONNECTION_OPEN ) {
		led = 0xE0;
	}
   	else if( event == ADB_CONNECTION_CLOSE ) {
		led = 0xC0;
	}
	IOWR_ALTERA_AVALON_PIO_DATA( PIO_OUT_BASE, led );
}

static void setup( void )
{
	// Initialise the ADB subsystem.  
	while( adbInit() ) {
		delay( 10 );
	}

	led = 0x80;
	IOWR_ALTERA_AVALON_PIO_DATA( PIO_OUT_BASE, led );

	while( 1 ) {
		connection = adbAddConnection( "tcp:4567", true, adbEventHandler );  
		if( connection != NULL ) {
			break;
		}
	}

	led = 0xC0;
	IOWR_ALTERA_AVALON_PIO_DATA( PIO_OUT_BASE, led );

	// Note start time
	lastTime = millis();
	count = 0;
}

static void loop( void )
{
	// Poll the ADB subsystem.
	adbPoll();

	if( connection != NULL ) {
		if( (millis() - lastTime) > 1000 ) {
			adbWrite( connection, 2, (uint8_t *)&count );
			count++;
			lastTime = millis();
		}
	}
}

int main( void )
{ 
	alt_putstr("Start\n");
	IOWR_ALTERA_AVALON_PIO_DATA( PIO_OUT_BASE, 0 );
	led = 0;

	setup();

	/* Event loop never exits. */
	while (1) {
		loop();
	}
}

