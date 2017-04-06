#ifndef _TS_QUEUE_H_INCLUDED_
#define _TS_QUEUE_H_INCLUDED_

typedef struct ts_queue_s ts_queue_t;

struct ts_queue_s {
  ts_queue_t *prev;
  ts_queue_t *next;
};

#define ts_queue_init(q) \
  (q)->prev = q;         \
  (q)->next = q

#define ts_queue_empty(h) (h == (h)->prev)

#define ts_queue_insert_head(h, x) \
  (x)->next       = (h)->next;     \
  (x)->next->prev = x;             \
  (x)->prev       = h;             \
  (h)->next       = x

#define ts_queue_insert_after ts_queue_insert_head

#define ts_queue_insert_tail(h, x) \
  (x)->prev       = (h)->prev;     \
  (x)->prev->next = x;             \
  (x)->next       = h;             \
  (h)->prev       = x

#define ts_queue_head(h) (h)->next

#define ts_queue_last(h) (h)->prev

#define ts_queue_sentinel(h) (h)

#define ts_queue_next(q) (q)->next

#define ts_queue_prev(q) (q)->prev

#if (TS_DEBUG)

#define ts_queue_remove(x)     \
  (x)->next->prev = (x)->prev; \
  (x)->prev->next = (x)->next; \
  (x)->prev       = NULL;      \
  (x)->next       = NULL

#else

#define ts_queue_remove(x)     \
  (x)->next->prev = (x)->prev; \
  (x)->prev->next = (x)->next

#endif

#define ts_queue_split(h, q, n) \
  (n)->prev       = (h)->prev;  \
  (n)->prev->next = n;          \
  (n)->next       = q;          \
  (h)->prev       = (q)->prev;  \
  (h)->prev->next = h;          \
  (q)->prev       = n;

#define ts_queue_add(h, n)     \
  (h)->prev->next = (n)->next; \
  (n)->next->prev = (h)->prev; \
  (h)->prev       = (n)->prev; \
  (h)->prev->next = h;

#define ts_queue_data(q, type, link) (type *)((u_char *)q - offsetof(type, link))

// ts_queue_t *ts_queue_middle(ts_queue_t *queue);
// void ts_queue_sort(ts_queue_t *queue,
//    int (*cmp)(const ts_queue_t *, const ts_queue_t *));

#endif /* _TS_QUEUE_H_INCLUDED_ */
