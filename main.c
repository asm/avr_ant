/* 
  Example implementation using avr_ant --
  This example was written for an Atmel ATMEGA168 with the clock division
  fuse cleared.
*/

#define F_CPU 8000000 // 8MHz
#define FOSC 8000000 // 8MHz

#include <stdlib.h>

#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "softuart.h"
#include "ant.h"

#define BAUD 4800
#define MYUBRR 103 // Calculated from http://www.wormfood.net/avrbaudcalc.php

// Define functions
//=======================
void ioinit(void);                             // initializes IO
void uart_putchar(char c);                     // sends char out of UART
uint8_t uart_getchar(void);                    // receives char from UART
void callback_broadcast_recv(uint8_t *buf, uint8_t len);
//=======================

static int my_stdio_putchar( char c, FILE *stream );
FILE suart_stream = FDEV_SETUP_STREAM( my_stdio_putchar, NULL, _FDEV_SETUP_WRITE );

void callback_broadcast_recv(uint8_t *buf, uint8_t len)
{
  uint8_t data[6];
  uint8_t i;

  /* Check if this is a NR stats message, if so, print it.
  /  buf[7] will hold the NR stat id:
     0: Apdex
     1: Error Rate
     2: Throughput
     3: Response Time
     4: DB
     5: CPU
     6: Memory
     buf[8] and buf[9] hold the value (little endian)
  */
  if (buf[6] == 0x2a) {
    printf("d: ");
    for (i = 6; i < len; i++) {
      printf("%x ", buf[i]);
    }
    printf("\n");
  }

  // At this point, we can transmit our data.
  // The protocol allows for 3 16 bit (little endian) data points.
  data[0] = 0x2a;   // Data 1
  data[1] = 0x00;
  data[2] = 0x2b;   // Data 2
  data[3] = 0x00;
  data[4] = 0x2c;   // Data 3
  data[5] = 0x00;

  ant_send_broadcast_data(1, data);
}

int main(void) {
  ant_configuration ant_config;

  ioinit();
    
  printf("\r\nBooting...\n");
  
  // Radio settings
  ant_config.address   = 1;
  ant_config.master    = FALSE;
  ant_config.frequency = 0x41;
  ant_config.period    = 2370;

  // Set callbacks
  ant_config.callback_broadcast_recv = &callback_broadcast_recv;

  // Initialize and configure ANT radio
  ant_init(ant_config);
  
  // Main loop
  while(1) {
    ant_handle_msg();
  }
}

void ioinit (void) {
  //1 = output, 0 = input
  DDRB = 0b11101111; //PB4 = MISO
  DDRC = 0b11111111; //all outputs
  DDRD = 0b11111110; //PORTD (RX on PD0)

  //USART Baud rate: 4800
  UBRR0H = (MYUBRR >> 8);
  UBRR0L = MYUBRR;
  UCSR0B = (1<<RXEN0)|(1<<TXEN0)|(1 << RXCIE0);

  softuart_init();
  softuart_turn_rx_off();
  sei();
  
  stdout = &suart_stream; //Required for printf init
}

static int my_stdio_putchar(char c, FILE *stream) {
  if ( c == '\n' ) {
    softuart_putchar( '\r' );
  }
  softuart_putchar( c );

  return 0;
}
