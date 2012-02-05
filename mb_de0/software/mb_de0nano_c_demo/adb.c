/*
	Copyright 2011 Niels Brouwers

	Licensed under the Apache License, Version 2.0 (the "License");
	you may not use this file except in compliance with the License.
	You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

	Unless required by applicable law or agreed to in writing, software
	distributed under the License is distributed on an "AS IS" BASIS,
	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
	See the License for the specific language governing permissions and
	limitations under the License.
*/

#include <string.h>
#include "wiring.h"
#include "adb.h"

// #define DEBUG

#define MAX_BUF_SIZE 256

enum {
	MAX_CONNECTION = 2,
};

static usb_device * adbDevice;
static boolean connected;
static int connectionLocalId = 1;

static Connection connections[MAX_CONNECTION];

// Event handler callback function.
adb_eventHandler * eventHandler;

// Forward declaration
static void usbEventHandler(usb_device * device, usb_eventType event);


static void fireEvent(Connection * connection, adb_eventType type, uint16_t length, uint8_t * data);
static int writeEmptyMessage(usb_device * device, uint32_t command, uint32_t arg0, uint32_t arg1);
static int writeMessage(usb_device * device, uint32_t command, uint32_t arg0, uint32_t arg1, uint32_t length, uint8_t * data);
static int writeStringMessage(usb_device * device, uint32_t command, uint32_t arg0, uint32_t arg1, char * str);
static boolean pollMessage(adb_message * message, boolean poll);
static void openClosedConnections(void);
static void handleOkay(Connection * connection, adb_message * message);
static void handleClose(Connection * connection);
static void handleWrite(Connection * connection, adb_message * message);
static void handleConnect(adb_message * message);
static boolean isAdbInterface(usb_interfaceDescriptor * interface);
static boolean isAdbDevice(usb_device * device, int configuration, adb_usbConfiguration * handle);
static void initUsb(usb_device * device, adb_usbConfiguration * handle);
static void closeAll( void );



/**
 * Initialises the ADB protocol. This function initialises the USB layer underneath so no further setup is required.
 */
int adbInit( void )
{
	uint8_t i;

	// Signal that we are not connected.
	adbDevice = NULL;
	connected = false;
	eventHandler = NULL;
	for( i = 0; i < MAX_CONNECTION; i++ ) {
		connections[i].status = ADB_UNUSED;
	}

	// Initialise the USB layer and attach an event handler.
	usbSetEventHandler( usbEventHandler );
	return usbInit();
}

/**
 * Sets the ADB event handler function. This function will be called by the ADB layer
 * when interesting events occur, such as ADB connect/disconnect, connection open/close, and
 * connection writes from the ADB device.
 *
 * @param handler event handler function.
 */
void adbSetEventHandler( adb_eventHandler * handler )
{
	eventHandler = handler;
}

/**
 * Fires an ADB event.
 * @param connection ADB connection. May be NULL in case of global connect/disconnect events.
 * @param type event type.
 * @param length payload length or zero if no payload.
 * @param data payload data if relevant or NULL otherwise.
 */
static void fireEvent( Connection *connection, adb_eventType type, uint16_t length, uint8_t *data )
{
	// Fire the global event handler, if set.
	if( eventHandler != NULL ) {
		eventHandler( connection, type, length, data );
	}

	// Fire the event handler of the connection in question, if relevant
	if( connection != NULL && connection->eventHandler != NULL ) {
		connection->eventHandler( connection, type, length, data );
	}
}

/**
 * Adds a new ADB connection. The connection string is per ADB specs, for example "tcp:1234" opens a
 * connection to tcp port 1234, and "shell:ls" outputs a listing of the phone root filesystem. Connections
 * can be made persistent by setting reconnect to true. Persistent connections will be automatically
 * reconnected when the USB cable is re-plugged in. Non-persistent connections will connect only once,
 * and should never be used after they are closed.
 *
 * The connection string is copied into the Connection record and may not exceed ADB_CONNECTIONSTRING_LENGTH-1
 * characters.
 *
 * @param connectionString ADB connectionstring. I.e. "tcp:1234" or "shell:ls".
 * @param reconnect true for automatic reconnect (persistent connections).
 * @param handler event handler.
 * @return an ADB connection record or NULL on failure (not enough slots or connection string too long).
 */
Connection *adbAddConnection( const char *connectionString, boolean reconnect, adb_eventHandler *handler )
{
	uint8_t i;
	Connection *connection;

	for( i = 0; i < MAX_CONNECTION; i++ ) {
		connection = &(connections[i]);
		if( connection->status == ADB_UNUSED ) {
			break;
		}
	}
	if( i == MAX_CONNECTION ) {
		return NULL;
	}

	if( 32 <= strlen( connectionString ) ) {
		return NULL;
	}

	// Initialise the newly created object.
	strncpy( connection->connectionString, connectionString, 32 );
	connection->localID = connectionLocalId++;
	connection->status = ADB_CLOSED;
	connection->lastConnectionAttempt = 0;
	connection->reconnect = reconnect;
	connection->eventHandler = handler;

	// Unable to find an empty spot, all connection slots in use.
	return connection;
}

/**
 * Prints an ADB_message, for debugging purposes.
 * @param message ADB message to print.
 */
#ifdef DEBUG
static void adb_printMessage( adb_message *message )
{
	switch( message->command ) {
	case A_OKAY:
		serialPrintf("OKAY message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	case A_CLSE:
		serialPrintf("CLSE message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	case A_WRTE:
		serialPrintf("WRTE message [%lx] %ld %ld, %ld bytes\n", message->command, message->arg0, message->arg1, message->data_length);
		break;
	case A_CNXN:
		serialPrintf("CNXN message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	case A_SYNC:
		serialPrintf("SYNC message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	case A_OPEN:
		serialPrintf("OPEN message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	default:
		serialPrintf("WTF message [%lx] %ld %ld\n", message->command, message->arg0, message->arg1);
		break;
	}
}
#endif

/**
 * Writes an empty message (without payload) to the ADB device.
 *
 * @param device USB device handle.
 * @param command ADB command.
 * @param arg0 first ADB argument (command dependent).
 * @param arg0 second ADB argument (command dependent).
 * @return error code or 0 for success.
 */
static int writeEmptyMessage( usb_device *device, uint32_t command, uint32_t arg0, uint32_t arg1 )
{
	adb_message message;

	message.command = command;
	message.arg0 = arg0;
	message.arg1 = arg1;
	message.data_length = 0;
	message.data_check = 0;
	message.magic = command ^ 0xffffffff;

#ifdef DEBUG
	serialPrint("OUT << "); adb_printMessage(&message);
#endif

	return usbBulkWrite( device, sizeof(adb_message), (uint8_t*)&message );
}

/**
 * Writes an ADB message with payload to the ADB device.
 *
 * @param device USB device handle.
 * @param command ADB command.
 * @param arg0 first ADB argument (command dependent).
 * @param arg0 second ADB argument (command dependent).
 * @param length payload length.
 * @param data command payload.
 * @return error code or 0 for success.
 */
static int writeMessage(usb_device * device, uint32_t command, uint32_t arg0, uint32_t arg1, uint32_t length, uint8_t * data)
{
	adb_message message;
	uint32_t count, sum = 0;
	uint8_t * x;
	uint8_t rcode;

	// Calculate data checksum
	count = length;
	x = data;
	while(count-- > 0) sum += *x++;

	// Fill out the message record.
	message.command = command;
	message.arg0 = arg0;
	message.arg1 = arg1;
	message.data_length = length;
	message.data_check = sum;
	message.magic = command ^ 0xffffffff;

#ifdef DEBUG
	serialPrint("OUT << "); adb_printMessage(&message);
#endif

	rcode = usbBulkWrite(device, sizeof(adb_message), (uint8_t*)&message);
	if( rcode ) return rcode;

	rcode = usbBulkWrite( device, length, data );
	return rcode;
}

/**
 * Writes an ADB command with a string as payload.
 *
 * @param device USB device handle.
 * @param command ADB command.
 * @param arg0 first ADB argument (command dependent).
 * @param arg0 second ADB argument (command dependent).
 * @param str payload string.
 * @return error code or 0 for success.
 */
static int writeStringMessage(usb_device * device, uint32_t command, uint32_t arg0, uint32_t arg1, char * str)
{
	return writeMessage(device, command, arg0, arg1, strlen(str) + 1, (uint8_t*)str);
}

/**
 * Poll an ADB message.
 * @param message on success, the ADB message will be returned in this struct.
 * @param poll true to poll for a packet on the input endpoint, false to wait for a packet. Use false here when a packet is expected (i.e. OKAY in response to WRTE)
 * @return true iff a packet was successfully received, false otherwise.
 */
static boolean pollMessage(
	adb_message *message,
	boolean poll )
{
	int bytesRead;
	uint8_t buf[ADB_USB_PACKETSIZE];

	// Poll a packet from the USB
	bytesRead = usbBulkRead( adbDevice, ADB_USB_PACKETSIZE, buf, poll );

	// Check if the USB in transfer was successful.
	if( bytesRead < 0 ) {
		return false;
	}

	// Check if the buffer contains a valid message
	memcpy( (void*)message, (void*)buf, sizeof(adb_message) );

	// If the message is corrupt, return.
	if( message->magic != (message->command ^ 0xffffffff) ) {
#ifdef DEBUG
		serialPrintf("Broken message, magic mismatch, %d bytes\n", bytesRead);
		return false;
#endif
	}

	// Check if the received number of bytes matches our expected 24 bytes of ADB message header.
	if( bytesRead != sizeof(adb_message) ) {
		return false;
	}

	return true;
}

/**
 * Sends an ADB OPEN message for any connections that are currently in the CLOSED state.
 */
static void openClosedConnections(void)
{
	int i;
	uint32_t timeSinceLastConnect;
	Connection *connection;

	// Iterate over the connection list and send "OPEN" for the ones that are currently closed.
	for( i = 0; i < MAX_CONNECTION; i++ ) {
		connection = connections + i;
		if( connection->status == ADB_UNUSED ) {
			continue;
		}

		timeSinceLastConnect = millis() - connection->lastConnectionAttempt;
		if( connection->status == ADB_CLOSED && timeSinceLastConnect > ADB_CONNECTION_RETRY_TIME ) {
			// Issue open command.
			writeStringMessage( adbDevice, A_OPEN, connection->localID, 0, connection->connectionString );

			// Record the last attempt time
			connection->lastConnectionAttempt = millis();
			connection->status = ADB_OPENING;
		}
	}
}

/**
 * Handles and ADB OKAY message, which represents a transition in the connection state machine.
 *
 * @param connection ADB connection
 * @param message ADB message struct.
 */
static void handleOkay( Connection *connection, adb_message * message )
{
	// Check if the OKAY message was a response to a CONNECT message.
	if( connection->status == ADB_OPENING ) {
		connection->status = ADB_OPEN;
		connection->remoteID = message->arg0;

		fireEvent( connection, ADB_CONNECTION_OPEN, 0, NULL );
	}
	// Check if the OKAY message was a response to a WRITE message.
	else if( connection->status == ADB_WRITING ) {
		connection->status = ADB_OPEN;
	}
}

/**
 * Handles an ADB CLOSE message, and fires an ADB event accordingly.
 *
 * @param connection ADB connection
 */
static void handleClose( Connection *connection )
{
	// Check if the CLOSE message was a response to a CONNECT message.
	if( connection->status == ADB_OPENING ) {
		fireEvent( connection, ADB_CONNECTION_FAILED, 0, NULL );
	}
	else {
		fireEvent( connection, ADB_CONNECTION_CLOSE, 0, NULL );
	}

	// Connection failed
	if( connection->reconnect ) {
		connection->status = ADB_CLOSED;
	}
	else {
		connection->status = ADB_UNUSED;
	}
}

/**
 * Handles an ADB WRITE message.
 *
 * @param connection ADB connection
 * @param message ADB message struct.
 */
static void handleWrite( Connection *connection, adb_message * message )
{
	uint32_t bytesLeft = message->data_length;
	uint8_t buf[ADB_USB_PACKETSIZE];
	ConnectionStatus previousStatus;
	int bytesRead;

	previousStatus = connection->status;

	connection->status = ADB_RECEIVING;
	connection->dataRead = 0;
	connection->dataSize = message->data_length;

	while( bytesLeft > 0 ) {
		int len = bytesLeft < ADB_USB_PACKETSIZE ? bytesLeft : ADB_USB_PACKETSIZE;

		// Read payload
		bytesRead = usbBulkRead( adbDevice, len, buf, false );

//		if (len != bytesRead)
//			serialPrintf("bytes read mismatch: %d expected, %d read, %ld left\n", len, bytesRead, bytesLeft);

		// Break out of the read loop if there's no data to read :(
		if( bytesRead == -1 ) {
			break;
		}

		connection->dataRead += len;
		fireEvent(connection, ADB_CONNECTION_RECEIVE, len, buf);

		bytesLeft -= bytesRead;
	}

	// Send OKAY message in reply.
	bytesRead = writeEmptyMessage(adbDevice, A_OKAY, message->arg1, message->arg0);

	connection->status = previousStatus;
}

/**
 * Close all ADB connections.
 *
 * @param connection ADB connection
 * @param message ADB message struct.
 */
static void closeAll( void )
{
	int i;
	Connection *connection;

	// Iterate over all connections and close the ones that are currently open.
	for( i = 0; i < MAX_CONNECTION; i++ ) {
		connection = &(connections[i]);
		if( connection->status == ADB_UNUSED ) {
			continue;
		}
		if( connection->status != ADB_CLOSED ) {
			handleClose( connection );
		}
	}
}

/**
 * Handles an ADB connect message. This is a response to a connect message sent from our side.
 * @param message ADB message.
 */
static void handleConnect( adb_message * message )
{
	unsigned int bytesRead;
	uint8_t buf[MAX_BUF_SIZE];
	uint16_t len;

	// Read payload (remote ADB device ID)
	len = message->data_length < MAX_BUF_SIZE ? message->data_length : MAX_BUF_SIZE;
	bytesRead = usbBulkRead( adbDevice, len, buf, false );

	// Signal that we are now connected to an Android device (yay!)
	connected = true;

	// Fire event.
	fireEvent( NULL, ADB_CONNECT, len, buf );
}

/**
 * This method is called periodically to check for new messages on the USB bus and process them.
 */
void adbPoll(void)
{
	int i;
	Connection *connection;
	adb_message message;

	// Poll the USB layer.
	usbPoll();

	// If no USB device, there's no work for us to be done, so just return.
	if( adbDevice == NULL ) return;

	// If not connected, send a connection string to the device.
	if( ! connected ) {
		writeStringMessage( adbDevice, A_CNXN, 0x01000000, 4096, (char*)"host::microbridge" );
		delay( 500 ); // Give the device some time to respond.
	}

	// If we are connected, check if there are connections that need to be opened
	if( connected ) {
		openClosedConnections();
	}

	// Check for an incoming ADB message.
	if( ! pollMessage( &message, true ) ) {
		return;
	}

	// Handle a response from the ADB device to our CONNECT message.
	if( message.command == A_CNXN ) {
		handleConnect( &message );
	}

	// Handle messages for specific connections
	for( i = 0; i < MAX_CONNECTION; i++ ) {
		connection = &(connections[i]);
		if( connection->status == ADB_UNUSED ) {
			continue;
		}
		if( connection->localID != message.arg1 ) {
			continue;
		}

		switch( message.command ) {
		case A_OKAY:
			handleOkay( connection, &message );
			break;
		case A_CLSE:
			handleClose( connection );
			break;
		case A_WRTE:
			handleWrite( connection, &message );
			break;
		default:
			break;
		}
	}
}

/**
 * Helper function for usb_isAdbDevice to check whether an interface is a valid ADB interface.
 * @param interface interface descriptor struct.
 */
static boolean isAdbInterface(usb_interfaceDescriptor * interface)
{

	// Check if the interface has exactly two endpoints.
	if (interface->bNumEndpoints!=2) return false;

	// Check if the endpoint supports bulk transfer.
	if (interface->bInterfaceProtocol != ADB_PROTOCOL) return false;
	if (interface->bInterfaceClass != ADB_CLASS) return false;
	if (interface->bInterfaceSubClass != ADB_SUBCLASS) return false;

	return true;
}

/**
 * Checks whether the a connected USB device is an ADB device and populates a configuration record if it is.
 *
 * @param device USB device.
 * @param handle pointer to a configuration record. The endpoint device address, configuration, and endpoint information will be stored here.
 * @return true iff the device is an ADB device.
 */
static boolean isAdbDevice( usb_device *device, int configuration, adb_usbConfiguration *handle )
{
	boolean ret = false;
	uint8_t buf[MAX_BUF_SIZE];
	int bytesRead;

	// Read the length of the configuration descriptor.
	bytesRead = usbGetConfigurationDescriptor( device, configuration, MAX_BUF_SIZE, buf );
	if( bytesRead < 0 ) return false;

	int pos = 0;
	uint8_t descriptorLength;
	uint8_t descriptorType;

	usb_configurationDescriptor * config = NULL;
	usb_interfaceDescriptor * interface = NULL;
	usb_endpointDescriptor * endpoint = NULL;

	while( pos < bytesRead ) {
		descriptorLength = buf[pos];
		descriptorType = buf[pos + 1];

		switch( descriptorType ) {
		case (USB_DESCRIPTOR_CONFIGURATION):
			config = (usb_configurationDescriptor *)(buf + pos);
			break;
		case (USB_DESCRIPTOR_INTERFACE):
			interface = (usb_interfaceDescriptor *)(buf + pos);

			if( isAdbInterface(interface) )
			{
				// handle->address = address;
				handle->configuration = config->bConfigurationValue;
				handle->interface = interface->bInterfaceNumber;

				// Detected ADB interface!
				ret = true;
			}
			break;
		case (USB_DESCRIPTOR_ENDPOINT):
			endpoint = (usb_endpointDescriptor *)(buf + pos);

			// If this endpoint descriptor is found right after the ADB interface descriptor, it belong to that interface.
			if (interface->bInterfaceNumber == handle->interface)
			{
				if (endpoint->bEndpointAddress & 0x80)
					handle->inputEndPointAddress = endpoint->bEndpointAddress & ~0x80;
				else
					handle->outputEndPointAddress = endpoint->bEndpointAddress;
			}

			break;
		default:
			break;
		}

		pos += descriptorLength;
	}

	return ret;

}

/**
 * Initialises an ADB device.
 *
 * @param device the USB device.
 * @param configuration configuration information.
 */
static void initUsb( usb_device *device, adb_usbConfiguration * handle )
{
	// Initialise/configure the USB device.
	// TODO write a usb_initBulkDevice function?
	usbInitDevice( device, handle->configuration );

	// Initialise bulk input endpoint.
	usbInitEndPoint( &(device->bulk_in), handle->inputEndPointAddress );
	device->bulk_in.attributes = USB_TRANSFER_TYPE_BULK;
	device->bulk_in.maxPacketSize = ADB_USB_PACKETSIZE;

	// Initialise bulk output endpoint.
	usbInitEndPoint( &(device->bulk_out), handle->outputEndPointAddress );
	device->bulk_out.attributes = USB_TRANSFER_TYPE_BULK;
	device->bulk_out.maxPacketSize = ADB_USB_PACKETSIZE;

	// Success, signal that we are now connected.
	adbDevice = device;
}

/**
 * Handles events from the USB layer.
 *
 * @param device USB device that generated the event.
 * @param event USB event.
 */
static void usbEventHandler( usb_device *device, usb_eventType event )
{
	adb_usbConfiguration handle;

//	alt_printf( "usbEvent %x\n", event );

	switch( event ) {
	case USB_CONNECT:

		// Check if the newly connected device is an ADB device, and initialise it if so.
		if( isAdbDevice( device, 0, &handle ) ) {
			initUsb( device, &handle );
		}
		break;

	case USB_DISCONNECT:

		// Check if the device that was disconnected is the ADB device we've been using.
		if( device == adbDevice ) {
			// Close all open ADB connections.
			closeAll();

			// Signal that we're no longer connected by setting the global device handler to NULL;
			adbDevice = NULL;
			connected = false;
		}
		break;

	default:
		// ignore
		break;
	}
}

/**
 * Write a set of bytes to an open ADB connection.
 *
 * @param connection ADB connection to write the data to.
 * @param length number of bytes to transmit.
 * @param data data to send.
 * @return number of transmitted bytes, or -1 on failure.
 */
int adbWrite(Connection * connection, uint16_t length, uint8_t * data)
{
	int ret;

	// First check if we have a working ADB connection
	if (adbDevice==NULL || !connected) return -1;

	// Check if the connection is open for writing.
	if (connection->status != ADB_OPEN) return -2;

	// Write payload
	ret = writeMessage(adbDevice, A_WRTE, connection->localID, connection->remoteID, length, data);
	if (ret==0)
		connection->status = ADB_WRITING;

	return ret;
}

/**
 * Write a string to an open ADB connection. The trailing zero is not transmitted.
 *
 * @param connection ADB connection to write the data to.
 * @param length number of bytes to transmit.
 * @param data data to send.
 * @return number of transmitted bytes, or -1 on failure.
 */
int adbWriteString(Connection * connection, char * str)
{
	int ret;

	// First check if we have a working ADB connection
	if (adbDevice==NULL || !connected) return -1;

	// Check if the connection is open for writing.
	if (connection->status != ADB_OPEN) return -2;

	// Write payload
	ret = writeStringMessage(adbDevice, A_WRTE, connection->localID, connection->remoteID, str);
	if (ret==0)
		connection->status = ADB_WRITING;

	return ret;
}

/**
 * Checks if the connection is open for writing.
 * @return true iff the connection is open and ready to accept write commands.
 */
bool adbConnectionIsOpen( Connection *connection )
{
	return connection->status == ADB_OPEN;
}

