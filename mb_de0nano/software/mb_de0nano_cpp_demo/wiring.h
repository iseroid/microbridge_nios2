#ifndef _WIRING_H_
#define _WIRING_H_

#include <stdint.h>

extern "C" {
	int alt_getchar();
	int alt_putchar(int c);
	int alt_putstr(const char* str);
	void alt_printf(const char *fmt, ...);
}

typedef bool boolean;

void delay( int );
uint32_t millis( void );


#endif
