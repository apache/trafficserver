#include "ts_lua_base_queue.h"

/*
 * find the middle queue element if the queue has odd number of elements
 * or the first element of the queue's second part otherwise
 */

ts_queue_t *
ts_queue_middle(ts_queue_t *queue)
{
  ts_queue_t *middle, *next;

  middle = ts_queue_head(queue);

  if (middle == ts_queue_last(queue)) {
    return middle;
  }

  next = ts_queue_head(queue);

  for (;;) {
    middle = ts_queue_next(middle);

    next = ts_queue_next(next);

    if (next == ts_queue_last(queue)) {
      return middle;
    }

    next = ts_queue_next(next);

    if (next == ts_queue_last(queue)) {
      return middle;
    }
  }
}

/* the stable insertion sort */

void
ts_queue_sort(ts_queue_t *queue, int (*cmp)(const ts_queue_t *, const ts_queue_t *))
{
  ts_queue_t *q, *prev, *next;

  q = ts_queue_head(queue);

  if (q == ts_queue_last(queue)) {
    return;
  }

  for (q = ts_queue_next(q); q != ts_queue_sentinel(queue); q = next) {
    prev = ts_queue_prev(q);
    next = ts_queue_next(q);

    ts_queue_remove(q);

    do {
      if (cmp(prev, q) <= 0) {
        break;
      }

      prev = ts_queue_prev(prev);

    } while (prev != ts_queue_sentinel(queue));

    ts_queue_insert_after(prev, q);
  }
}
