#include <stdio.h>

typedef struct ring_buffer
{
  void *buffer;      // data buffer
  void *buffer_end;  // end of data buffer
  uint8_t capacity;  // maximum number of items in the buffer
  uint8_t count;     // number of items in the buffer
  void *head;        // pointer to head
  void *tail;        // pointer to tail
} ring_buffer;

typedef struct pop_val
{
  uint8_t byte;
  uint8_t success;
} pop_value;

void rb_init(volatile ring_buffer *rb, uint8_t size, void *buffer);
void rb_push(volatile ring_buffer *rb, const uint8_t byte);
pop_value rb_pop(volatile ring_buffer *rb);