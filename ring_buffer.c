#include "ring_buffer.h"
#include <avr/io.h>

void rb_init(volatile ring_buffer *rb, uint8_t size, void *buffer)
{
  rb->buffer = buffer;
  rb->buffer_end = (uint8_t *)rb->buffer + size;
  rb->count = 0;
  rb->head = rb->buffer;
  rb->tail = rb->buffer;
}

void rb_push(volatile ring_buffer *rb, const uint8_t byte)
{
  *(uint8_t*)rb->head = byte;
  rb->head = (char*)rb->head + 1;
  if(rb->head == rb->buffer_end)
  rb->head = rb->buffer;
  rb->count++;
}

pop_value rb_pop(volatile ring_buffer *rb)
{
  pop_value value;
  
  if(rb->count == 0) {
    value.success = 0;
    return value;
  }	

  value.byte = *(uint8_t*)rb->tail;
  value.success = 1;
  
  rb->tail = (uint8_t*)rb->tail + 1;
  if(rb->tail == rb->buffer_end)
  rb->tail = rb->buffer;
  rb->count--;
  
  return value;
}