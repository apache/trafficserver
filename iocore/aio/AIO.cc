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

/*
 * Async Disk IO operations.
 */

#include "tscore/TSSystemState.h"
#include "tscore/ink_hw.h"

#include "P_AIO.h"

#if AIO_MODE == AIO_MODE_NATIVE
#define AIO_PERIOD -HRTIME_MSECONDS(10)
#else

#define MAX_DISKS_POSSIBLE 100

// globals

int ts_config_with_inkdiskio = 0;
/* structure to hold information about each file descriptor */
AIO_Reqs *aio_reqs[MAX_DISKS_POSSIBLE];
/* number of unique file descriptors in the aio_reqs array */
int num_filedes = 1;

// acquire this mutex before inserting a new entry in the aio_reqs array.
// Don't need to acquire this for searching the array
static ink_mutex insert_mutex;

int thread_is_created = 0;
#endif // AIO_MODE == AIO_MODE_NATIVE
RecInt cache_config_threads_per_disk = 12;
RecInt api_config_threads_per_disk   = 12;

RecRawStatBlock *aio_rsb       = nullptr;
Continuation *aio_err_callback = nullptr;
// AIO Stats
uint64_t aio_num_read      = 0;
uint64_t aio_bytes_read    = 0;
uint64_t aio_num_write     = 0;
uint64_t aio_bytes_written = 0;

/*
 * Stats
 */

static int
aio_stats_cb(const char * /* name ATS_UNUSED */, RecDataT data_type, RecData *data, RecRawStatBlock *rsb, int id)
{
  (void)data_type;
  (void)rsb;
  int64_t new_val = 0;
  int64_t diff    = 0;
  int64_t count, sum;
  ink_hrtime now = ink_get_hrtime();
  // The RecGetGlobalXXX stat functions are cheaper than the
  // RecGetXXX functions. The Global ones are expensive
  // for increments and decrements. But for AIO stats we
  // only do Sets and Gets, so they are cheaper in our case.
  RecGetGlobalRawStatSum(aio_rsb, id, &sum);
  RecGetGlobalRawStatCount(aio_rsb, id, &count);

  int64_t time_diff = ink_hrtime_to_msec(now - count);
  if (time_diff == 0) {
    data->rec_float = 0.0;
    return 0;
  }
  switch (id) {
  case AIO_STAT_READ_PER_SEC:
    new_val = aio_num_read;
    break;
  case AIO_STAT_WRITE_PER_SEC:
    new_val = aio_num_write;
    break;
  case AIO_STAT_KB_READ_PER_SEC:
    new_val = aio_bytes_read >> 10;
    break;
  case AIO_STAT_KB_WRITE_PER_SEC:
    new_val = aio_bytes_written >> 10;
    break;
  default:
    ink_assert(0);
  }
  diff = new_val - sum;
  RecSetGlobalRawStatSum(aio_rsb, id, new_val);
  RecSetGlobalRawStatCount(aio_rsb, id, now);
  data->rec_float = static_cast<float>(diff) * 1000.00 / static_cast<float>(time_diff);
  return 0;
}

#ifdef AIO_STATS
/* total number of requests received - for debugging */
static int num_requests = 0;
/* performance results */
static AIOTestData *data;

int
AIOTestData::ink_aio_stats(int event, void *d)
{
  ink_hrtime now   = ink_get_hrtime();
  double time_msec = (double)(now - start) / (double)HRTIME_MSECOND;
  int i            = (aio_reqs[0] == nullptr) ? 1 : 0;
  for (; i < num_filedes; ++i)
    printf("%0.2f\t%i\t%i\t%i\n", time_msec, aio_reqs[i]->filedes, aio_reqs[i]->pending, aio_reqs[i]->queued);
  printf("Num Requests: %i Num Queued: %i num Moved: %i\n\n", data->num_req, data->num_queue, data->num_temp);
  eventProcessor.schedule_in(this, HRTIME_MSECONDS(50), ET_CALL);
  return EVENT_DONE;
}

#endif // AIO_STATS

/*
 * Common
 */
AIOCallback *
new_AIOCallback()
{
  return new AIOCallbackInternal;
}

void
ink_aio_set_err_callback(Continuation *callback)
{
  aio_err_callback = callback;
}

void
ink_aio_init(ts::ModuleVersion v)
{
  ink_release_assert(v.check(AIO_MODULE_INTERNAL_VERSION));

  aio_rsb = RecAllocateRawStatBlock(static_cast<int>(AIO_STAT_COUNT));
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.read_per_sec", RECD_FLOAT, RECP_PERSISTENT,
                     (int)AIO_STAT_READ_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.write_per_sec", RECD_FLOAT, RECP_PERSISTENT,
                     (int)AIO_STAT_WRITE_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.KB_read_per_sec", RECD_FLOAT, RECP_PERSISTENT,
                     (int)AIO_STAT_KB_READ_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.KB_write_per_sec", RECD_FLOAT, RECP_PERSISTENT,
                     (int)AIO_STAT_KB_WRITE_PER_SEC, aio_stats_cb);
#if AIO_MODE != AIO_MODE_NATIVE
  memset(&aio_reqs, 0, MAX_DISKS_POSSIBLE * sizeof(AIO_Reqs *));
  ink_mutex_init(&insert_mutex);
#endif
  REC_ReadConfigInteger(cache_config_threads_per_disk, "proxy.config.cache.threads_per_disk");
#if TS_USE_LINUX_NATIVE_AIO
  Warning("Running with Linux AIO, there are known issues with this feature");
#endif
}

int
ink_aio_start()
{
#ifdef AIO_STATS
  data = new AIOTestData();
  eventProcessor.schedule_in(data, HRTIME_MSECONDS(100), ET_CALL);
#endif
  return 0;
}

#if AIO_MODE != AIO_MODE_NATIVE

static void *aio_thread_main(void *arg);

struct AIOThreadInfo : public Continuation {
  AIO_Reqs *req;
  int sleep_wait;

  int
  start(int event, Event *e)
  {
    (void)event;
    (void)e;
#if TS_USE_HWLOC
#if HWLOC_API_VERSION >= 0x20000
    hwloc_set_membind(ink_get_topology(), hwloc_topology_get_topology_nodeset(ink_get_topology()), HWLOC_MEMBIND_INTERLEAVE,
                      HWLOC_MEMBIND_THREAD | HWLOC_MEMBIND_BYNODESET);
#else
    hwloc_set_membind_nodeset(ink_get_topology(), hwloc_topology_get_topology_nodeset(ink_get_topology()), HWLOC_MEMBIND_INTERLEAVE,
                              HWLOC_MEMBIND_THREAD);
#endif
#endif
    aio_thread_main(this);
    delete this;
    return EVENT_DONE;
  }

  AIOThreadInfo(AIO_Reqs *thr_req, int sleep) : Continuation(new_ProxyMutex()), req(thr_req), sleep_wait(sleep)
  {
    SET_HANDLER(&AIOThreadInfo::start);
  }
};

/*
  A dedicated number of threads (THREADS_PER_DISK) wait on the condition
  variable associated with the file descriptor. The cache threads try to put
  the request in the appropriate queue. If they fail to acquire the lock, they
  put the request in the atomic list.
 */

/* insert  an entry for file descriptor fildes into aio_reqs */
static AIO_Reqs *
aio_init_fildes(int fildes, int fromAPI = 0)
{
  char thr_name[MAX_THREAD_NAME_LENGTH];
  int i;
  AIO_Reqs *request = new AIO_Reqs;

  INK_WRITE_MEMORY_BARRIER;

  ink_cond_init(&request->aio_cond);
  ink_mutex_init(&request->aio_mutex);

  RecInt thread_num;

  if (fromAPI) {
    request->index    = 0;
    request->filedes  = -1;
    aio_reqs[0]       = request;
    thread_is_created = 1;
    thread_num        = api_config_threads_per_disk;
  } else {
    request->index        = num_filedes;
    request->filedes      = fildes;
    aio_reqs[num_filedes] = request;
    thread_num            = cache_config_threads_per_disk;
  }

  /* create the main thread */
  AIOThreadInfo *thr_info;
  size_t stacksize;

  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");
  for (i = 0; i < thread_num; i++) {
    if (i == (thread_num - 1)) {
      thr_info = new AIOThreadInfo(request, 1);
    } else {
      thr_info = new AIOThreadInfo(request, 0);
    }
    snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ET_AIO %d:%d]", i, fildes);
    ink_assert(eventProcessor.spawn_thread(thr_info, thr_name, stacksize));
  }

  /* the num_filedes should be incremented after initializing everything.
     This prevents a thread from looking at uninitialized fields */
  if (!fromAPI) {
    num_filedes++;
  }
  return request;
}

/* insert a request into aio_todo queue. */
static void
aio_insert(AIOCallback *op, AIO_Reqs *req)
{
#ifdef AIO_STATS
  num_requests++;
  req->queued++;
#endif
  req->aio_todo.enqueue(op);
}

/* move the request from the atomic list to the queue */
static void
aio_move(AIO_Reqs *req)
{
  if (req->aio_temp_list.empty()) {
    return;
  }

  AIOCallbackInternal *cbi;
  SList(AIOCallbackInternal, alink) aq(req->aio_temp_list.popall());

  // flip the list
  Queue<AIOCallback> cbq;
  while ((cbi = aq.pop())) {
    cbq.push(cbi);
  }

  AIOCallback *cb;
  while ((cb = cbq.pop())) {
    aio_insert(cb, req);
  }
}

/* queue the new request */
static void
aio_queue_req(AIOCallbackInternal *op, int fromAPI = 0)
{
  int thread_ndx = 1;
  AIO_Reqs *req  = op->aio_req;
  op->link.next  = nullptr;
  op->link.prev  = nullptr;
#ifdef AIO_STATS
  ink_atomic_increment((int *)&data->num_req, 1);
#endif
  if (!fromAPI && (!req || req->filedes != op->aiocb.aio_fildes)) {
    /* search for the matching file descriptor */
    for (; thread_ndx < num_filedes; thread_ndx++) {
      if (aio_reqs[thread_ndx]->filedes == op->aiocb.aio_fildes) {
        /* found the matching file descriptor */
        req = aio_reqs[thread_ndx];
        break;
      }
    }
    if (!req) {
      ink_mutex_acquire(&insert_mutex);
      if (thread_ndx == num_filedes) {
        /* insert a new entry */
        req = aio_init_fildes(op->aiocb.aio_fildes);
      } else {
        /* a new entry was inserted between the time we checked the
           aio_reqs and acquired the mutex. check the aio_reqs array to
           make sure the entry inserted does not correspond  to the current
           file descriptor */
        for (thread_ndx = 1; thread_ndx < num_filedes; thread_ndx++) {
          if (aio_reqs[thread_ndx]->filedes == op->aiocb.aio_fildes) {
            req = aio_reqs[thread_ndx];
            break;
          }
        }
        if (!req) {
          req = aio_init_fildes(op->aiocb.aio_fildes);
        }
      }
      ink_mutex_release(&insert_mutex);
    }
    op->aio_req = req;
  }
  if (fromAPI && (!req || req->filedes != -1)) {
    ink_mutex_acquire(&insert_mutex);
    if (aio_reqs[0] == nullptr) {
      req = aio_init_fildes(-1, 1);
    } else {
      req = aio_reqs[0];
    }
    ink_mutex_release(&insert_mutex);
    op->aio_req = req;
  }
  ink_atomic_increment(&req->requests_queued, 1);
  if (!ink_mutex_try_acquire(&req->aio_mutex)) {
#ifdef AIO_STATS
    ink_atomic_increment(&data->num_temp, 1);
#endif
    req->aio_temp_list.push(op);
  } else {
/* check if any pending requests on the atomic list */
#ifdef AIO_STATS
    ink_atomic_increment(&data->num_queue, 1);
#endif
    aio_move(req);
    /* now put the new request */
    aio_insert(op, req);
    ink_cond_signal(&req->aio_cond);
    ink_mutex_release(&req->aio_mutex);
  }
}

static inline int
cache_op(AIOCallbackInternal *op)
{
  bool read = (op->aiocb.aio_lio_opcode == LIO_READ);
  for (; op; op = (AIOCallbackInternal *)op->then) {
    ink_aiocb *a = &op->aiocb;
    ssize_t err, res = 0;

    while (a->aio_nbytes - res > 0) {
      do {
        if (read) {
          err = pread(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res, a->aio_offset + res);
        } else {
          err = pwrite(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res, a->aio_offset + res);
        }
      } while ((err < 0) && (errno == EINTR || errno == ENOBUFS || errno == ENOMEM));
      if (err <= 0) {
        Warning("cache disk operation failed %s %zd %d\n", (a->aio_lio_opcode == LIO_READ) ? "READ" : "WRITE", err, errno);
        op->aio_result = -errno;
        return (err);
      }
      res += err;
    }
    op->aio_result = res;
    ink_assert(op->ok());
  }
  return 1;
}

int
ink_aio_read(AIOCallback *op, int fromAPI)
{
  op->aiocb.aio_lio_opcode = LIO_READ;
  aio_queue_req((AIOCallbackInternal *)op, fromAPI);

  return 1;
}

int
ink_aio_write(AIOCallback *op, int fromAPI)
{
  op->aiocb.aio_lio_opcode = LIO_WRITE;
  aio_queue_req((AIOCallbackInternal *)op, fromAPI);

  return 1;
}

bool
ink_aio_thread_num_set(int thread_num)
{
  if (thread_num > 0 && !thread_is_created) {
    api_config_threads_per_disk = thread_num;
    return true;
  }

  return false;
}

void *
aio_thread_main(void *arg)
{
  AIOThreadInfo *thr_info = static_cast<AIOThreadInfo *>(arg);
  AIO_Reqs *my_aio_req    = thr_info->req;
  AIO_Reqs *current_req   = nullptr;
  AIOCallback *op         = nullptr;
  ink_mutex_acquire(&my_aio_req->aio_mutex);
  for (;;) {
    do {
      if (TSSystemState::is_event_system_shut_down()) {
        ink_mutex_release(&my_aio_req->aio_mutex);
        return nullptr;
      }
      current_req = my_aio_req;
      /* check if any pending requests on the atomic list */
      aio_move(my_aio_req);
      if (!(op = my_aio_req->aio_todo.pop())) {
        break;
      }
#ifdef AIO_STATS
      num_requests--;
      current_req->queued--;
      ink_atomic_increment((int *)&current_req->pending, 1);
#endif
      // update the stats;
      if (op->aiocb.aio_lio_opcode == LIO_WRITE) {
        aio_num_write++;
        aio_bytes_written += op->aiocb.aio_nbytes;
      } else {
        aio_num_read++;
        aio_bytes_read += op->aiocb.aio_nbytes;
      }
      ink_mutex_release(&current_req->aio_mutex);
      cache_op((AIOCallbackInternal *)op);
      ink_atomic_increment(&current_req->requests_queued, -1);
#ifdef AIO_STATS
      ink_atomic_increment((int *)&current_req->pending, -1);
#endif
      op->link.prev = nullptr;
      op->link.next = nullptr;
      op->mutex     = op->action.mutex;
      if (op->thread == AIO_CALLBACK_THREAD_AIO) {
        SCOPED_MUTEX_LOCK(lock, op->mutex, thr_info->mutex->thread_holding);
        op->handleEvent(EVENT_NONE, nullptr);
      } else if (op->thread == AIO_CALLBACK_THREAD_ANY) {
        eventProcessor.schedule_imm(op);
      } else {
        op->thread->schedule_imm(op);
      }
      ink_mutex_acquire(&my_aio_req->aio_mutex);
    } while (true);
    timespec timedwait_msec = ink_hrtime_to_timespec(ink_get_hrtime() + HRTIME_MSECONDS(net_config_poll_timeout));
    ink_cond_timedwait(&my_aio_req->aio_cond, &my_aio_req->aio_mutex, &timedwait_msec);
  }
  return nullptr;
}
#else
int
DiskHandler::startAIOEvent(int /* event ATS_UNUSED */, Event *e)
{
  SET_HANDLER(&DiskHandler::mainAIOEvent);
  e->schedule_every(AIO_PERIOD);
  trigger_event = e;
  return EVENT_CONT;
}

int
DiskHandler::mainAIOEvent(int event, Event *e)
{
  AIOCallback *op = nullptr;
Lagain:
  int ret = io_getevents(ctx, 0, MAX_AIO_EVENTS, events, nullptr);
  for (int i = 0; i < ret; i++) {
    op             = (AIOCallback *)events[i].data;
    op->aio_result = events[i].res;
    ink_assert(op->action.continuation);
    complete_list.enqueue(op);
  }

  if (ret == MAX_AIO_EVENTS) {
    goto Lagain;
  }

  if (ret < 0) {
    if (errno == EINTR)
      goto Lagain;
    if (errno == EFAULT || errno == ENOSYS)
      Debug("aio", "io_getevents failed: %s (%d)", strerror(-ret), -ret);
  }

  ink_aiocb *cbs[MAX_AIO_EVENTS];
  int num = 0;

  for (; num < MAX_AIO_EVENTS && ((op = ready_list.dequeue()) != nullptr); ++num) {
    cbs[num] = &op->aiocb;
    ink_assert(op->action.continuation);
  }

  if (num > 0) {
    int ret;
    do {
      ret = io_submit(ctx, num, cbs);
    } while (ret < 0 && ret == -EAGAIN);

    if (ret != num) {
      if (ret < 0) {
        Debug("aio", "io_submit failed: %s (%d)", strerror(-ret), -ret);
      } else {
        Fatal("could not submit IOs, io_submit(%p, %d, %p) returned %d", ctx, num, cbs, ret);
      }
    }
  }

  while ((op = complete_list.dequeue()) != nullptr) {
    op->mutex = op->action.mutex;
    MUTEX_TRY_LOCK(lock, op->mutex, trigger_event->ethread);
    if (!lock.is_locked()) {
      trigger_event->ethread->schedule_imm(op);
    } else {
      op->handleEvent(EVENT_NONE, nullptr);
    }
  }
  return EVENT_CONT;
}

int
ink_aio_read(AIOCallback *op, int /* fromAPI ATS_UNUSED */)
{
  op->aiocb.aio_lio_opcode = IO_CMD_PREAD;
  op->aiocb.data           = op;
  EThread *t               = this_ethread();
#ifdef HAVE_EVENTFD
  io_set_eventfd(&op->aiocb, t->evfd);
#endif
  t->diskHandler->ready_list.enqueue(op);

  return 1;
}

int
ink_aio_write(AIOCallback *op, int /* fromAPI ATS_UNUSED */)
{
  op->aiocb.aio_lio_opcode = IO_CMD_PWRITE;
  op->aiocb.data           = op;
  EThread *t               = this_ethread();
#ifdef HAVE_EVENTFD
  io_set_eventfd(&op->aiocb, t->evfd);
#endif
  t->diskHandler->ready_list.enqueue(op);

  return 1;
}

int
ink_aio_readv(AIOCallback *op, int /* fromAPI ATS_UNUSED */)
{
  EThread *t      = this_ethread();
  DiskHandler *dh = t->diskHandler;
  AIOCallback *io = op;
  int sz          = 0;

  while (io) {
    io->aiocb.aio_lio_opcode = IO_CMD_PREAD;
    io->aiocb.data           = io;
#ifdef HAVE_EVENTFD
    io_set_eventfd(&op->aiocb, t->evfd);
#endif
    dh->ready_list.enqueue(io);
    ++sz;
    io = io->then;
  }

  if (sz > 1) {
    ink_assert(op->action.continuation);
    AIOVec *vec = new AIOVec(sz, op);
    while (--sz >= 0) {
      op->action = vec;
      op         = op->then;
    }
  }
  return 1;
}

int
ink_aio_writev(AIOCallback *op, int /* fromAPI ATS_UNUSED */)
{
  EThread *t      = this_ethread();
  DiskHandler *dh = t->diskHandler;
  AIOCallback *io = op;
  int sz          = 0;

  while (io) {
    io->aiocb.aio_lio_opcode = IO_CMD_PWRITE;
    io->aiocb.data           = io;
#ifdef HAVE_EVENTFD
    io_set_eventfd(&op->aiocb, t->evfd);
#endif
    dh->ready_list.enqueue(io);
    ++sz;
    io = io->then;
  }

  if (sz > 1) {
    ink_assert(op->action.continuation);
    AIOVec *vec = new AIOVec(sz, op);
    while (--sz >= 0) {
      op->action = vec;
      op         = op->then;
    }
  }
  return 1;
}
#endif // AIO_MODE != AIO_MODE_NATIVE
