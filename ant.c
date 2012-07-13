/* Copyright (c) 2012, Jason Snell
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.

   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.

   * Neither the name of the copyright holders nor the names of
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE. */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "ant.h"
#include "ring_buffer.h"

// Globals
uint8_t rx_buf[MAXMSG];

// Ring buffer
uint8_t buffer[255];
volatile ring_buffer rx_ring;

// Config
static ant_configuration _config;

// Internal prototypes
//=======================
static void ant_config(void);
static void dispatch_msg(uint8_t len);
static void reset (void);
static void get_capabilities(void);
static void assign_channel_id(uint8_t type);
static void set_channel_id(void);
static void timeout(uint8_t timeout);
static void set_frequency(uint8_t frequency);
static void set_channel_period(uint16_t period);
static void open_channel(void);
static uint8_t checksum(uint8_t *data, uint8_t length);
static void uart_putchar(char c);
static void send_to_ant(uint8_t* buffer, uint8_t len);
static void delay_ms(uint16_t x);
static void print_msg(uint8_t len);
//=======================

// USART RX interrupt handler
ISR(USART_RX_vect) {
  rb_push(&rx_ring, UDR0);
}

void ant_handle_msg(void)
{
  uint8_t msg_n = 0;;
  uint8_t in_msg = FALSE;
  
  pop_value value;
  
  while(1) {
    value = rb_pop(&rx_ring);

    // Nothing to read and we're not waiting on a message
    if (value.success == 0 && in_msg == FALSE)
    return;

    // We must be waiting for a byte
    if (value.success == 0 && in_msg == TRUE)
    continue;

    if ((value.byte == MESG_TX_SYNC) && (in_msg == FALSE)) {
      msg_n = 0;  // Always reset when we receive a sync header
      in_msg = TRUE;
      rx_buf[msg_n] = value.byte;
      msg_n++;
    } else if (msg_n == 1) {
      // Size
      rx_buf[msg_n] = value.byte;
      msg_n++;
    } else if (msg_n == 2) {
      // Type
      rx_buf[msg_n] = value.byte;
      msg_n++;
    } else if (msg_n < rx_buf[1] + 3) {
      rx_buf[msg_n] = value.byte;
      msg_n++;
    } else {
      in_msg = FALSE;
      rx_buf[msg_n] = value.byte;

      if (checksum(rx_buf, msg_n) == rx_buf[msg_n])
      {
        dispatch_msg(msg_n);
      } else {
        printf("checksum failed\n");
      }
      break;
    }
  }
}

void dispatch_msg(uint8_t len)
{
  switch(rx_buf[2])
  {
    case MESG_RESPONSE_EVENT_ID:
      if(rx_buf[4] == 1)  // Channel Events
      {
        switch(rx_buf[5])
        {
          case RESPONSE_NO_ERROR:
            return;
          case EVENT_RX_SEARCH_TIMEOUT:
            printf("EVENT_RX_SEARCH_TIMEOUT, re-opening channel...\n");
            ant_config();
            return;
          case EVENT_RX_FAIL:
            // Not great, but not the end of the world
            return;
          case EVENT_TX:
            if (_config.callback_event_tx > 0)
            {
              _config.callback_event_tx();
            }
            return;
          default:
            print_msg(len);
            return;
        }
      } else {  // Function Responses
        print_msg(len);
        return;
      }
      return;
    case MESG_BROADCAST_DATA_ID:
      if (_config.callback_broadcast_recv > 0)
      {
        _config.callback_broadcast_recv(rx_buf, len);
      }
      return;
    default:    // No idea what this is...
      print_msg(len);
      return;
  }
}

void print_msg(uint8_t len)
{
  uint8_t i;

  printf("m: %x - ", rx_buf[2]);
  for (i = 3; i < len; i++) {
    printf("%x ", rx_buf[i]);
  }
  printf("\n");
}

void ant_send_broadcast_data(uint16_t addr, uint8_t *data)
{
  uint8_t buf[13];
  
  buf[0] = MESG_TX_SYNC;           // SYNC Byte
  buf[1] = 0x09;                   // Length Byte
  buf[2] = MESG_BROADCAST_DATA_ID; // ID Byte
  buf[3] = CHAN0;                  // Channel #
  buf[4] = addr & 255;
  buf[5] = addr >> 8;

  buf[6]  = data[0];               // Note that data must always be 6 bytes
  buf[7]  = data[1];
  buf[8]  = data[2];
  buf[9]  = data[3];
  buf[10] = data[4];
  buf[11] = data[5];
  buf[12] = checksum(buf, 12);
  
  send_to_ant(buf, 13);

  printf("MESG_BROADCAST_DATA_ID sent\n");
}

void ant_send_acknowledged_data(uint16_t addr, uint8_t *data)
{
  uint8_t buf[13];
  
  buf[0] = MESG_TX_SYNC;              // SYNC Byte
  buf[1] = 0x09;                      // Length Byte
  buf[2] = MESG_ACKNOWLEDGED_DATA_ID; // ID Byte
  buf[3] = CHAN0;                     // Channel #
  buf[4] = addr & 255;
  buf[5] = addr >> 8;

  buf[6]  = data[0];               // Note that data must always be 6 bytes
  buf[7]  = data[1];
  buf[8]  = data[2];
  buf[9]  = data[3];
  buf[10] = data[4];
  buf[11] = data[5];
  buf[12] = checksum(buf, 12);
  
  send_to_ant(buf, 13);

  printf("MESG_ACKNOWLEDGED_DATA_ID sent\n");
}

void ant_config(void)
{
  uint8_t data[6];

  if (_config.master == TRUE)
  {
    assign_channel_id(0x30);  // Channel type (0x30 == shared transmit channel
  } else {
    assign_channel_id(0x20);  // Channel type (0x20 == shared receive channel
  }
  ant_handle_msg();

  set_channel_id();
  ant_handle_msg();

  set_channel_period(_config.period);
  ant_handle_msg();

  set_frequency(_config.frequency);
  ant_handle_msg();

  open_channel();
  ant_handle_msg();

  // If we're not a master, tell the ANT radio what our address is
  if (_config.master == FALSE)
  {
    data[0] = 1;
    data[1] = 1;
    data[2] = 1;
    data[3] = 1;
    data[4] = 1;
    data[5] = 1;
    
    ant_send_broadcast_data(_config.address, data);
  }
}

void ant_init(ant_configuration config)
{
  rb_init(&rx_ring, 255, buffer);

  _config = config;
  
  reset();
  delay_ms(600);

  ant_config();
}

void reset(void)
{
  uint8_t buf[5];
  
  buf[0] = MESG_TX_SYNC;         // SYNC Byte
  buf[1] = 0x01;                 // Length Byte
  buf[2] = MESG_SYSTEM_RESET_ID; // ID Byte
  buf[3] = 0x00;                 // Data Byte N (N=Length)
  buf[4] = checksum(buf, 4);
  
  send_to_ant(buf, 5);

  printf("MESG_SYSTEM_RESET_ID sent\n");
}


void get_capabilities(void)
{
  uint8_t buf[5];
  
  buf[0] = MESG_TX_SYNC;          // SYNC Byte
  buf[1] = 0x02;                  // Length Byte
  buf[2] = MESG_REQUEST_ID;       // ID Byte
  buf[3] = CHAN0;                 // Channel
  buf[4] = MESG_CAPABILITIES_ID;  // Data Byte N (N=Length)
  buf[5] = checksum(buf, 5);
  
  send_to_ant(buf, 6);

  printf("MESG_CAPABILITIES_ID sent\n");

  ant_handle_msg();
}

void assign_channel_id(uint8_t type)
{
  uint8_t buf[7];
  
  buf[0] = MESG_TX_SYNC;           // SYNC Byte
  buf[1] = 0x03;                   // Length Byte
  buf[2] = MESG_ASSIGN_CHANNEL_ID; // ID Byte
  buf[3] = CHAN0;                  // Channel
  buf[4] = type;                   // Type
  buf[5] = NET0;                   // Network
  buf[6] = checksum(buf, 6);
  
  send_to_ant(buf, 7);

  printf("MESG_ASSIGN_CHANNEL_ID sent\n");

  ant_handle_msg();
}

void set_channel_id(void)
{
  uint8_t buf[9];
  
  buf[0] = MESG_TX_SYNC;       // SYNC Byte
  buf[1] = 0x05;               // Length Byte
  buf[2] = MESG_CHANNEL_ID_ID; // ID Byte
  buf[3] = CHAN0;              // Channel number
  buf[4] = 0x01;               // Device id (2 bytes, little endian)
  buf[5] = 0x00;
  buf[6] = 0x03;               // Device type
  buf[7] = 0x03;               // Transmission type
  buf[8] = checksum(buf, 8);

  send_to_ant(buf, 9);

  printf("MESG_CHANNEL_ID_ID sent\n");

  ant_handle_msg();
}

void timeout(uint8_t timeout)
{
  uint8_t buf[6];
  
  buf[0] = MESG_TX_SYNC;                   // SYNC Byte
  buf[1] = 0x02;                           // Length Byte
  buf[2] = MESG_CHANNEL_SEARCH_TIMEOUT_ID; // ID Byte
  buf[3] = CHAN0;                          // Channel
  buf[4] = timeout;
  buf[5] = checksum(buf, 5);

  send_to_ant(buf, 6);

  printf("MESG_CHANNEL_SEARCH_TIMEOUT_ID sent\n");

  ant_handle_msg();
}

void set_frequency(uint8_t frequency)
{
  uint8_t buf[6];
  
  buf[0] = MESG_TX_SYNC;               // SYNC Byte
  buf[1] = 0x02;                       // Length Byte
  buf[2] = MESG_CHANNEL_RADIO_FREQ_ID; // ID Byte
  buf[3] = CHAN0;                      // Channel
  buf[4] = frequency;
  buf[5] = checksum(buf,5);

  send_to_ant(buf, 6);

  printf("MESG_CHANNEL_RADIO_FREQ_ID sent\n");

  ant_handle_msg();
}

void set_channel_period(uint16_t period)
{
  uint8_t buf[7];
  
  buf[0] = MESG_TX_SYNC;                // SYNC Byte
  buf[1] = 0x03;                        // Length Byte
  buf[2] = MESG_CHANNEL_MESG_PERIOD_ID; // ID Byte
  buf[3] = CHAN0;                       // Channel
  buf[4] = period & 255;                // LSB
  buf[5] = period >> 8;                 // MSB
  buf[6] = checksum(buf, 6);

  send_to_ant(buf, 7);

  printf("MESG_CHANNEL_MESG_PERIOD_ID sent\n");

  ant_handle_msg();
}

void open_channel(void)
{
  uint8_t buf[5];
  
  buf[0] = MESG_TX_SYNC;         // SYNC Byte
  buf[1] = 0x01;                 // Length Byte
  buf[2] = MESG_OPEN_CHANNEL_ID; // ID Byte
  buf[3] = CHAN0;                // Channel
  buf[4] = checksum(buf, 4);

  send_to_ant(buf, 5);

  printf("MESG_OPEN_CHANNEL_ID sent\n");

  ant_handle_msg();
}

void send_to_ant(uint8_t* buffer, uint8_t len)
{
  uint8_t i;

  for(i = 0; i < len; i++) {
    uart_putchar(buffer[i]);
  }
}

void uart_putchar(char c) {
  while ( !( UCSR0A & (1<<UDRE0)) );
  UDR0 = c;
}

uint8_t checksum(uint8_t *data, uint8_t length)
{
  uint8_t i;
  uint8_t chksum = data[0];
  
  for (i = 1; i < length; i++)
    chksum ^= data[i];  // +1 since skip prefix sync code, we already counted it
  
  return chksum;
}

//General short delays
void delay_ms(uint16_t x) {
  uint8_t y, z;

  for ( ; x > 0 ; x--) {
    for ( y = 0 ; y < 90 ; y++) {
      for ( z = 0 ; z < 6 ; z++) {
        asm volatile ("nop");
      }
    }
  }
}
