/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include <inkaio.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/signal.h>

int libinkaio_mmap = 1;

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

pthread_mutex_t kcblist_m = PTHREAD_MUTEX_INITIALIZER;
static struct kcb_list
{
  struct kcb_list *n;
  INKAIOCB *kcb;
} *kcblist;

static void
inkaio_die(void)
{
  struct kcb_list *l;

        /*-
		we don't lock because we're in exit().  we don't free
		stuff because we're in exit().  we simply want to close()
		our fds down in order to shut down our kernel threads.
	 */
  for (l = kcblist; l; l = l->n)
    close(l->kcb->fd);
  return;
}

INKAIOCB *
__inkaio_create_shared(int bufsiz, int (*callback) (kcall_t *))
{
  INKAIOCB *kcb;
  struct kcb_list *l;
  static int first = 1;

  if (bufsiz < INKAIO_MIN_MMAP_SIZE)
    bufsiz = INKAIO_MIN_MMAP_SIZE;
  if ((kcb = calloc(1, sizeof *kcb)) == 0)
    return 0;
  if ((l = malloc(sizeof *l)) == 0) {
    free(kcb);
    return 0;
  }
  kcb->shared = 1;
  kcb->size = bufsiz;
  kcb->readsize = bufsiz;
  kcb->maxsize = bufsiz;
  if ((kcb->fd = open(INKAIO_DEV, O_RDWR | O_NONBLOCK)) == -1) {
    free(kcb);
    return 0;
  }
  kcb->outbuf = kcb->outptr = mmap(0, kcb->size, PROT_READ | PROT_WRITE, MAP_PRIVATE, kcb->fd, 0);
  if (kcb->outptr == MAP_FAILED) {
    free(l);
    free(kcb);
    close(kcb->fd);
    return 0;
  }
  kcb->eoob = kcb->outbuf + kcb->size;
  kcb->callback = callback;
  kcb->done = (aio_mem_t *) mmap(0, kcb->readsize, PROT_READ, MAP_PRIVATE, kcb->fd, 0);
  if (kcb->done == MAP_FAILED) {
    munmap(kcb->outbuf, kcb->size);
    free(l);
    free(kcb);
    close(kcb->fd);
    return 0;
  }
  pthread_mutex_init(&kcb->mutex, 0);
  pthread_mutex_lock(&kcblist_m);
  l->n = kcblist;
  l->kcb = kcb;
  kcblist = l;
  if (first) {
    first = 0;
    atexit(&inkaio_die);
  }
  pthread_mutex_unlock(&kcblist_m);
  return kcb;
}

INKAIOCB *
__inkaio_create(int maxbufsiz, int (*callback) (kcall_t *))
{
  INKAIOCB *kcb;
  struct kcb_list *l;
  static int first = 1;

  if (maxbufsiz == 0)
    maxbufsiz = 4 * 1024 * 1024;        /* 4M? */
  else if (maxbufsiz < 4096)
    maxbufsiz = 4096;
  if ((kcb = malloc(sizeof *kcb)) == 0)
    return 0;
  if ((l = malloc(sizeof *l)) == 0) {
    free(kcb);
    return 0;
  }
  if ((kcb->fd = open(INKAIO_DEV, O_RDWR | O_NONBLOCK)) == -1) {
    free(l);
    free(kcb);
    return 0;
  }
  kcb->shared = 0;
  kcb->size = 4096;
  kcb->readsize = kcb->size;
  kcb->maxsize = maxbufsiz;
  kcb->readsize = kcb->size * 2;
  kcb->callback = callback;
  kcb->outbuf = kcb->outptr = mmap(0, kcb->size, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
  if (kcb->outptr == MAP_FAILED) {
    free(l);
    free(kcb);
    close(kcb->fd);
    return 0;
  }
  kcb->eoob = kcb->outbuf + kcb->size;
  kcb->done = (aio_mem_t *) mmap(0, kcb->maxsize, PROT_READ, MAP_PRIVATE, kcb->fd, 0);
  if (kcb->done == MAP_FAILED) {
    munmap(kcb->outbuf, kcb->size);
    free(l);
    free(kcb);
    close(kcb->fd);
    return 0;
  }
  pthread_mutex_init(&kcb->mutex, 0);
  pthread_mutex_lock(&kcblist_m);
  l->n = kcblist;
  l->kcb = kcb;
  kcblist = l;
  if (first) {
    first = 0;
    atexit(&inkaio_die);
  }
  pthread_mutex_unlock(&kcblist_m);
  return kcb;
}

INKAIOCB *
inkaio_create(int maxbufsiz, int (*callback) (kcall_t *))
{
  if (libinkaio_mmap || maxbufsiz < 0)
    return __inkaio_create_shared(maxbufsiz, callback);
  else
    return __inkaio_create(maxbufsiz, callback);
}

int
inkaio_destroy(INKAIOCB * kcb)
{
  struct kcb_list *l, *lp = 0;

  if (kcb == 0)
    return 0;

  pthread_mutex_lock(&kcblist_m);
  for (l = kcblist; l; l = l->n) {
    if (l->kcb == kcb) {
      if (lp)
        lp->n = l->n;
      else
        kcblist = l->n;
      free(l);
      break;
    }
    lp = l;
  }
  pthread_mutex_unlock(&kcblist_m);
  pthread_mutex_lock(&kcb->mutex);
  close(kcb->fd);
  munmap(kcb->outbuf, kcb->size);       /* leaky? */
  pthread_mutex_unlock(&kcb->mutex);
  free(kcb);
  return 0;
}

/* called with mutex locked */
__inline__ int
__inkaio_results(INKAIOCB * kcb, int serial, kcall_t * k)
{
  int len, from_start;
  kcall_t *res;
  char *user, *end;
  unsigned offset;

  if ((len = k->len) == 0)
    return 0;
  from_start = (unsigned) k->value;
  offset = (unsigned) k->cookie;

  /* set up a INKAIO_RESULTS event for the kernel */
  if (serial != kcb->serial || __inkaio_space(kcb, &res, 0, 0) == -1)
    return 1;
  res->type = INKAIO_RESULTS;
  res->cookie = 0;
  res->value = 0;
  res->len = 0;

  user = (char *) kcb->done + offset;
  end = user + len;
again:
#if 0
  printf("__inkaio_results: INKAIO_RESULTS len %d, from_start %d, offset %d\n", len, from_start, offset);
  printf("__inkaio_results: done %p, start %p, user %p, end %p, end-user %d\n", kcb->done,
         (char *) kcb->done + sizeof *kcb->done, user, end, end - user);
#endif
  /* walk available results */
  while (user < end) {
    int klen;
    kcall_t *k;

    k = (kcall_t *) user;
    klen = sizeof *k + (k->len > 0 ? k->len : 0);
#if 0
    printf("__inkaio_results: k %p, type %d, len %d, value %d, cookie %x, klen %d\n", k, k->type, k->len, k->value,
           k->cookie, klen);
#endif

    if (k->len == -1 || (k->len == 0 && k->value < 0))
      errno = -k->value;

    res->value += klen;
    user += klen;
    pthread_mutex_unlock(&kcb->mutex);
    kcb->callback(k);
    pthread_mutex_lock(&kcb->mutex);
    if (serial != kcb->serial) {
                        /*-
				callback called dispatch;
				we should drop our work since
				a lower dispatch got newer
				results.
			 */
      return 1;
    }
  }
  if (from_start) {
    user = (char *) kcb->done + sizeof *kcb->done;
    end = user + from_start;
    from_start = 0;
    goto again;
  }
  return 0;
}

int
inkaio_dispatch(INKAIOCB * kcb)
{
  kcall_t *k;
  int serial;
  int reqlen, i;

  if (kcb->shared == 0) {
    inkaio_execute(kcb);
    inkaio_returns(kcb);
    pthread_mutex_lock(&kcb->mutex);
    reqlen = kcb->outptr - kcb->outbuf;
    pthread_mutex_unlock(&kcb->mutex);
    return reqlen;
  }
  pthread_mutex_lock(&kcb->mutex);
  serial = ++kcb->serial;
  reqlen = kcb->outptr - kcb->outbuf;
  i = ioctl(kcb->fd, INKAIO_IOCTL_DISPATCH, reqlen);
  if (i == -1) {
    pthread_mutex_unlock(&kcb->mutex);
    return -1;
  }
  if (i > reqlen) {
    fprintf(stderr, "inkaio_dispatch(kernel executed more than we passed in!? %d > %d)\n", i, reqlen);
    exit(1);
  }
  reqlen -= i;
  kcb->outptr = kcb->outbuf + reqlen;
  if (reqlen) {
    /* we should always be able to send everything down */
    memmove(kcb->outbuf, kcb->outbuf + i, reqlen);
    pthread_mutex_unlock(&kcb->mutex);
    return reqlen;
  }
  k = (kcall_t *) kcb->outbuf;
  if (k->type == INKAIO_RESULTS)
    __inkaio_results(kcb, serial, k);
  reqlen = kcb->outptr - kcb->outbuf;
  pthread_mutex_unlock(&kcb->mutex);
  return reqlen;
}

int
inkaio_submit(INKAIOCB * kcb)
{
  int reqlen, len;

  if (kcb->shared == 0)
    return -1;

  pthread_mutex_lock(&kcb->mutex);
  reqlen = kcb->outptr - kcb->outbuf;

  kcb->serial++;
  len = ioctl(kcb->fd, INKAIO_IOCTL_SUBMIT, reqlen);
  if (len == -1) {
    pthread_mutex_unlock(&kcb->mutex);
    return -1;
  }
  reqlen -= len;
  if (reqlen)
    memmove(kcb->outbuf, kcb->outbuf + len, reqlen);
  kcb->outptr = kcb->outbuf + reqlen;
  pthread_mutex_unlock(&kcb->mutex);
  return reqlen;
}

int
inkaio_execute(INKAIOCB * kcb)
{
  int reqlen, len;

  pthread_mutex_lock(&kcb->mutex);
  reqlen = kcb->outptr - kcb->outbuf;
  if (reqlen) {
    len = write(kcb->fd, kcb->outbuf, reqlen);
    if (len == -1) {
      pthread_mutex_unlock(&kcb->mutex);
      return -1;
    }
    reqlen -= len;
    if (reqlen)
      memmove(kcb->outbuf, kcb->outbuf + len, reqlen);
    kcb->outptr = kcb->outbuf + reqlen;
  }
  pthread_mutex_unlock(&kcb->mutex);
  return reqlen;
}

int
inkaio_returns(INKAIOCB * kcb)
{
  kcall_t *k;
  char *buf, *end;
  int i, nops = 0;

  if ((buf = alloca(kcb->readsize)) == 0) {
    kcb->readsize /= 2;
    if (kcb->readsize < 4096)
      kcb->readsize = 4096;
    if ((buf = alloca(kcb->readsize)) == 0)
      return -1;
  }
  if ((i = read(kcb->fd, buf, kcb->readsize)) <= 0)
    return 0;
  else if (i == kcb->readsize)
    kcb->readsize *= 2;
  end = buf + i;
  while (buf < end) {
    int klen;

    k = (kcall_t *) buf;
    klen = sizeof *k + (k->len > 0 ? k->len : 0);

    if (k->len == -1 || (k->len == 0 && k->value < 0))
      errno = -k->value;

    kcb->callback(k);
    buf += klen;
    nops++;
  }
  return nops;
}

/* this must be called with the mutex locked */
int
__inkaio_space(INKAIOCB * kcb, kcall_t ** k, void **data, int len)
{
  int offset;

  /* room for kcall_t & data */
  while (kcb->outptr + sizeof **k + len > kcb->eoob) {
    int newsize;
    char *p;

    if (kcb->size >= kcb->maxsize) {
      kcall_t k;

      k.type = INKAIO_FLUSH;
      k.cookie = kcb;
      k.len = 0;
      k.value = 0;
      pthread_mutex_unlock(&kcb->mutex);
      kcb->callback(&k);
      pthread_mutex_lock(&kcb->mutex);
      continue;
    }
    offset = (unsigned) kcb->outptr - (unsigned) kcb->outbuf;
    newsize = kcb->size + 4096;
    p = mmap(kcb->outbuf, newsize, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);
    if (p == MAP_FAILED)
      return -1;
    if (p != kcb->outbuf) {
      memmove(p, kcb->outbuf, offset);
      munmap(kcb->outbuf, kcb->size);
      kcb->outbuf = p;
      kcb->outptr = p + offset;
    }
    kcb->size = newsize;
    kcb->eoob = kcb->outbuf + newsize;
  }
  *k = (void *) kcb->outptr;
  if (data)
    *data = (void *) (kcb->outptr + sizeof **k);
  kcb->outptr += sizeof **k + len;
  return 0;
}

int
inkaio_aioread(INKAIOCB * kcb, void *cookie, int fd, void *buf, size_t count, loff_t offset)
{
  struct _kcall *k;
  struct aio_preadpwrite_in *kr;

  pthread_mutex_lock(&kcb->mutex);
  if (__inkaio_space(kcb, &k, (void **) &kr, sizeof *kr) == -1) {
    pthread_mutex_unlock(&kcb->mutex);
    return -1;
  }
  k->type = INKAIO_ASYNC_READ;
  k->cookie = cookie;
  k->len = sizeof *kr;
  k->value = 0;
  kr->fd = fd;
  kr->ptr = buf;
  kr->len = count;
  kr->offset = offset;
  pthread_mutex_unlock(&kcb->mutex);
  return 0;
}

int
inkaio_aiowrite(INKAIOCB * kcb, void *cookie, int fd, void *buf, size_t count, loff_t offset)
{
  struct _kcall *k;
  struct aio_preadpwrite_in *kw;

  pthread_mutex_lock(&kcb->mutex);
  if (__inkaio_space(kcb, &k, (void **) &kw, sizeof *kw) == -1) {
    pthread_mutex_unlock(&kcb->mutex);
    return -1;
  }
  k->type = INKAIO_ASYNC_WRITE;
  k->cookie = cookie;
  k->len = sizeof *kw;
  k->value = 0;
  kw->fd = fd;
  kw->ptr = buf;
  kw->len = count;
  kw->offset = offset;
  pthread_mutex_unlock(&kcb->mutex);
  return 0;
}
