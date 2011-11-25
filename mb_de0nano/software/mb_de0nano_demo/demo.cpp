#include "wiring.h"
#include "adb.h"
#include <stdio.h>

extern "C" {
	void setup( void );
	void loop( void );
	void alt_printf(const char *fmt, ...);
}
// Adb connection.
Connection * connection;

// Elapsed time for ADC sampling
long lastTime;

static uint16_t count;

// Event handler for the shell connection. 
void adbEventHandler(Connection * connection, adb_eventType event, uint16_t length, uint8_t * data)
{
	// Data packets contain two bytes, one for each servo, in the range of [0..180]
	if (event == ADB_CONNECTION_RECEIVE) {
		alt_printf( "Rx %x %x\n", data[0], data[1] );
	}
}

void setup()
{
	// Note start time
	lastTime = millis();

	// Initialise the ADB subsystem.  
	ADB::init();

	connection = NULL;
	count = 0;

}

void loop()
{
	if( connection == NULL ) {
		// Open an ADB stream to the phone's shell. Auto-reconnect
		connection = ADB::addConnection("tcp:4567", true, adbEventHandler);  
	}
	if( connection == NULL ) {
		return;
	}

	if ((millis() - lastTime) > 1000)
	{
		connection->write(2, (uint8_t*)&count);
		count++;
		lastTime = millis();
	}

	// Poll the ADB subsystem.
	ADB::poll();
}

