#ifndef _WIRING_H_
#define _WIRING_H_

#include <stdint.h>
#include "sys/alt_stdio.h"

typedef unsigned char bool;
typedef unsigned char boolean;

enum {
	false,
	true
};

void delay( int );
uint32_t millis( void );


#endif
