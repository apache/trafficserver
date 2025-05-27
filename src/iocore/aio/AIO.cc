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

#include "iocore/aio/AIO.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/EventProcessor.h"
#include "records/RecCore.h"
#include "records/RecDefs.h"
#include "tscore/TSSystemState.h"
#include "tscore/ink_atomic.h"
#if TS_USE_HWLOC
#include "tscore/ink_hw.h"
#endif

#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"
#endif

#ifdef AIO_FAULT_INJECTION
#include "iocore/aio/AIO_fault_injection.h"
#endif

#define MAX_DISKS_POSSIBLE 100

// globals
static constexpr ts::ModuleVersion AIO_MODULE_INTERNAL_VERSION{AIO_MODULE_PUBLIC_VERSION, ts::ModuleVersion::PRIVATE};

// for debugging
// #define AIO_STATS 1

#if TS_USE_LINUX_IO_URING
static bool use_io_uring = false;

namespace
{
void setup_prep_ops(IOUringContext *);
}
#endif

/* structure to hold information about each file descriptor */
struct AIO_Reqs;
AIO_Reqs *aio_reqs[MAX_DISKS_POSSIBLE];
/* number of unique file descriptors in the aio_reqs array */
int num_filedes = 1;

// acquire this mutex before inserting a new entry in the aio_reqs array.
// Don't need to acquire this for searching the array
static ink_mutex insert_mutex;

int thread_is_created = 0;

RecInt cache_config_threads_per_disk = 12;
RecInt api_config_threads_per_disk   = 12;

Continuation *aio_err_callback = nullptr;

/* internal definitions */
int
AIOCallback::io_complete(int event, void *data)
{
  (void)event;
  (void)data;
  if (aio_err_callback && !ok()) {
    AIOCallback *err_op          = new AIOCallback();
    err_op->aiocb.aio_fildes     = this->aiocb.aio_fildes;
    err_op->aiocb.aio_lio_opcode = this->aiocb.aio_lio_opcode;
    err_op->mutex                = aio_err_callback->mutex;
    err_op->action               = aio_err_callback;

    // Take this lock in-line because we want to stop other I/O operations on this disk ASAP
    SCOPED_MUTEX_LOCK(lock, aio_err_callback->mutex, this_ethread());
    err_op->action.continuation->handleEvent(EVENT_NONE, err_op);
  }
  if (!action.cancelled && action.continuation) {
    action.continuation->handleEvent(AIO_EVENT_DONE, this);
  }
  return EVENT_DONE;
}

struct AIO_Reqs {
  Que(AIOCallback, link) aio_todo; /* queue for AIO operations */
                                   /* Atomic list to temporarily hold the request if the
                                      lock for a particular queue cannot be acquired */
  ASLL(AIOCallback, alink) aio_temp_list;
  ink_mutex aio_mutex;
  ink_cond  aio_cond;
  int       index           = 0;  /* position of this struct in the aio_reqs array */
  int       pending         = 0;  /* number of outstanding requests on the disk */
  int       queued          = 0;  /* total number of aio_todo requests */
  int       filedes         = -1; /* the file descriptor for the requests or status IO_NOT_IN_PROGRESS */
  int       requests_queued = 0;
};

#ifdef AIO_STATS
class AIOTestData : public Continuation
{
public:
  int        num_req;
  int        num_temp;
  int        num_queue;
  ink_hrtime start;

  int ink_aio_stats(int event, void *data);

  AIOTestData() : Continuation(new_ProxyMutex()), num_req(0), num_temp(0), num_queue(0)
  {
    start = ink_get_hrtime();
    SET_HANDLER(&AIOTestData::ink_aio_stats);
  }
};
#endif

struct AIOStatsBlock {
  ts::Metrics::Counter::AtomicType *read_count;
  ts::Metrics::Counter::AtomicType *kb_read;
  ts::Metrics::Counter::AtomicType *write_count;
  ts::Metrics::Counter::AtomicType *kb_write;
};

AIOStatsBlock aio_rsb;

#ifdef AIO_STATS
/* total number of requests received - for debugging */
static int num_requests = 0;
/* performance results */
static AIOTestData *data;

int
AIOTestData::ink_aio_stats(int event, void *d)
{
  ink_hrtime now       = ink_get_hrtime();
  double     time_msec = (double)(now - start) / (double)HRTIME_MSECOND;
  int        i         = (aio_reqs[0] == nullptr) ? 1 : 0;
  for (; i < num_filedes; ++i) {
    printf("%0.2f\t%i\t%i\t%i\n", time_msec, aio_reqs[i]->filedes, aio_reqs[i]->pending, aio_reqs[i]->queued);
  }
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
  return new AIOCallback;
}

void
ink_aio_set_err_callback(Continuation *callback)
{
  aio_err_callback = callback;
}

void
ink_aio_init(ts::ModuleVersion v, [[maybe_unused]] AIOBackend backend)
{
  ink_release_assert(v.check(AIO_MODULE_INTERNAL_VERSION));

  aio_rsb.read_count  = ts::Metrics::Counter::createPtr("proxy.process.cache.aio.read_count");
  aio_rsb.write_count = ts::Metrics::Counter::createPtr("proxy.process.cache.aio.write_count");
  aio_rsb.kb_read     = ts::Metrics::Counter::createPtr("proxy.process.cache.aio.KB_read");
  aio_rsb.kb_write    = ts::Metrics::Counter::createPtr("proxy.process.cache.aio.KB_write");

  memset(&aio_reqs, 0, MAX_DISKS_POSSIBLE * sizeof(AIO_Reqs *));
  ink_mutex_init(&insert_mutex);

  cache_config_threads_per_disk = RecGetRecordInt("proxy.config.cache.threads_per_disk").value_or(0);

#if TS_USE_LINUX_IO_URING
  // If the caller specified auto backend, check for config to force a backend
  if (backend == AIOBackend::AIO_BACKEND_AUTO) {
    auto aio_mode{RecGetRecordStringAlloc("proxy.config.aio.mode")};
    if (aio_mode && !aio_mode->empty()) {
      if (strcasecmp(*aio_mode, "auto") == 0) {
        backend = AIOBackend::AIO_BACKEND_AUTO;
      } else if (strcasecmp(*aio_mode, "thread") == 0) {
        // force thread mode
        backend = AIOBackend::AIO_BACKEND_THREAD;
      } else if (strcasecmp(*aio_mode, "io_uring") == 0) {
        // force io_uring mode
        backend = AIOBackend::AIO_BACKEND_IO_URING;
      } else {
        Warning("Invalid value '%s' for proxy.config.aio.mode.  autodetecting", aio_mode->c_str());
      }
    }
  }

  switch (backend) {
  case AIOBackend::AIO_BACKEND_AUTO: {
    // detect if io_uring is available and can support the required features
    auto *ctx = IOUringContext::local_context();
    if (ctx && ctx->valid()) {
      // check to see which ops we can use (this can't fail)
      setup_prep_ops(ctx);
      use_io_uring = true;
    } else {
      Note("AIO using thread backend as required io_uring ops are not supported");
      use_io_uring = false;
    }
    break;
  }
  case AIOBackend::AIO_BACKEND_IO_URING:
    use_io_uring = true;
    break;
  case AIOBackend::AIO_BACKEND_THREAD:
    use_io_uring = false;
    break;
  }

  if (use_io_uring) {
    Note("Using io_uring for AIO");
  } else {
    Note("Using thread for AIO");
  }
#endif
}

struct AIOThreadInfo : public Continuation {
  AIO_Reqs *req;
  int       sleep_wait;
  void     *aio_thread_main(AIOThreadInfo *thr_info);

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
  char      thr_name[MAX_THREAD_NAME_LENGTH];
  int       i;
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
  size_t         stacksize;

  stacksize = RecGetRecordInt("proxy.config.thread.default.stacksize").value_or(0);
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

  AIOCallback *cbi;
  SList(AIOCallback, alink) aq(req->aio_temp_list.popall());

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
aio_queue_req(AIOCallback *op, int fromAPI = 0)
{
  int       thread_ndx = 1;
  AIO_Reqs *req        = op->aio_req;
  op->link.next        = nullptr;
  op->link.prev        = nullptr;
#ifdef AIO_STATS
  ink_atomic_increment(&data->num_req, 1);
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
cache_op(AIOCallback *op)
{
  bool read = (op->aiocb.aio_lio_opcode == LIO_READ);
  for (; op; op = op->then) {
    ink_aiocb *a = &op->aiocb;
    ssize_t    err, res = 0;

    while (a->aio_nbytes - res > 0) {
      do {
        if (read) {
#ifdef AIO_FAULT_INJECTION
          err = aioFaultInjection.pread(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res,
                                        a->aio_offset + res);
#else
          err = pread(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res, a->aio_offset + res);
#endif
        } else {
#ifdef AIO_FAULT_INJECTION
          err = aioFaultInjection.pwrite(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res,
                                         a->aio_offset + res);
#else
          err = pwrite(a->aio_fildes, (static_cast<char *>(a->aio_buf)) + res, a->aio_nbytes - res, a->aio_offset + res);
#endif
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
AIOThreadInfo::aio_thread_main(AIOThreadInfo *thr_info)
{
  AIO_Reqs    *my_aio_req = thr_info->req;
  AIOCallback *op         = nullptr;
  ink_mutex_acquire(&my_aio_req->aio_mutex);
  for (;;) {
    do {
      if (TSSystemState::is_event_system_shut_down()) {
        ink_mutex_release(&my_aio_req->aio_mutex);
        return nullptr;
      }
      /* check if any pending requests on the atomic list */
      aio_move(my_aio_req);
      if (!(op = my_aio_req->aio_todo.pop())) {
        break;
      }
#ifdef AIO_STATS
      num_requests--;
      my_aio_req->queued--;
      ink_atomic_increment(&my_aio_req->pending, 1);
#endif
      ink_mutex_release(&my_aio_req->aio_mutex);

      // update the stats;
      if (op->aiocb.aio_lio_opcode == LIO_WRITE) {
        ts::Metrics::Counter::increment(aio_rsb.write_count);
        ts::Metrics::Counter::increment(aio_rsb.kb_write, op->aiocb.aio_nbytes >> 10);
      } else {
        ts::Metrics::Counter::increment(aio_rsb.read_count);
        ts::Metrics::Counter::increment(aio_rsb.kb_read, op->aiocb.aio_nbytes >> 10);
      }
      cache_op(reinterpret_cast<AIOCallback *>(op));
      ink_atomic_increment(&my_aio_req->requests_queued, -1);
#ifdef AIO_STATS
      ink_atomic_increment(&my_aio_req->pending, -1);
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
    timespec timedwait_msec = ink_hrtime_to_timespec(ink_get_hrtime() + HRTIME_MSECONDS(EThread::default_wait_interval_ms));
    ink_cond_timedwait(&my_aio_req->aio_cond, &my_aio_req->aio_mutex, &timedwait_msec);
  }
  return nullptr;
}

#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"

namespace
{

void
prep_read(io_uring_sqe *sqe, AIOCallback *op)
{
  io_uring_prep_read(sqe, op->aiocb.aio_fildes, op->aiocb.aio_buf, op->aiocb.aio_nbytes, op->aiocb.aio_offset);
}

void
prep_readv(io_uring_sqe *sqe, AIOCallback *op)
{
  op->iov.iov_len  = op->aiocb.aio_nbytes;
  op->iov.iov_base = op->aiocb.aio_buf;
  io_uring_prep_readv(sqe, op->aiocb.aio_fildes, &op->iov, 1, op->aiocb.aio_offset);
}

void
prep_write(io_uring_sqe *sqe, AIOCallback *op)
{
  io_uring_prep_write(sqe, op->aiocb.aio_fildes, op->aiocb.aio_buf, op->aiocb.aio_nbytes, op->aiocb.aio_offset);
}

void
prep_writev(io_uring_sqe *sqe, AIOCallback *op)
{
  op->iov.iov_len  = op->aiocb.aio_nbytes;
  op->iov.iov_base = op->aiocb.aio_buf;
  io_uring_prep_writev(sqe, op->aiocb.aio_fildes, &op->iov, 1, op->aiocb.aio_offset);
}

using prep_op = void (*)(io_uring_sqe *, AIOCallback *);

prep_op prep_ops[] = {
  nullptr,
  prep_readv,
  prep_writev,
};

/*
 * The default io_uring ops are readv/writev as those were available since the first io_uring.
 * This function checks for normal read/write support and changes to those if available.
 */
void
setup_prep_ops(IOUringContext *ur)
{
  if (!ur->supports_op(IORING_OP_READ)) {
    prep_ops[LIO_READ]  = prep_read;
    prep_ops[LIO_WRITE] = prep_write;
  }
}

void
io_uring_prep_ops_internal(AIOCallback *op_in, int op_type)
{
  IOUringContext *ur = IOUringContext::local_context();
  AIOCallback    *op = op_in;
  while (op) {
    op->this_op       = op;
    io_uring_sqe *sqe = ur->next_sqe(op);

    ink_release_assert(sqe != nullptr);

    prep_ops[op_type](sqe, op);

    op->aiocb.aio_lio_opcode = op_type;
    if (op->then) {
      sqe->flags |= IOSQE_IO_LINK;
    } else if (op->aio_op == nullptr) { // This condition leaves an existing aio_op in place if there is one. (EAGAIN)
      op->aio_op = op_in;
    }

    op = op->then;
  }
}

} // namespace

void
AIOCallback::handle_complete(io_uring_cqe *cqe)
{
  AIOCallback *op = this_op;

  // Re-submit the request on EAGAIN.
  // we might need to re-submit the entire rest of the chain, so just call prep again
  // The manpage seems to indicate that the rest of the SQEs in the chain will not execute on an error.
  if (cqe->res == -EAGAIN) {
    io_uring_prep_ops_internal(op, op->aiocb.aio_lio_opcode);
    return;
  }

  // if this was canceled, then we can ignore it.  It was probably resubmitted after an EAGAIN
  if (cqe->res == -ECANCELED) {
    return;
  }

  op->aio_result = static_cast<int64_t>(cqe->res);
  op->link.prev  = nullptr;
  op->link.next  = nullptr;
  op->mutex      = op->action.mutex;

  if (op->aio_result > 0) {
    if (op->aiocb.aio_lio_opcode == LIO_WRITE) {
      ts::Metrics::Counter::increment(aio_rsb.write_count);
      ts::Metrics::Counter::increment(aio_rsb.kb_write, op->aiocb.aio_nbytes >> 10);
    } else {
      ts::Metrics::Counter::increment(aio_rsb.read_count);
      ts::Metrics::Counter::increment(aio_rsb.kb_read, op->aiocb.aio_nbytes >> 10);
    }
  }

  // the last op in the linked ops will have the original op stored in the aiocb
  if (aio_op) {
    op = op->aio_op;
    if (op->thread == AIO_CALLBACK_THREAD_AIO) {
      SCOPED_MUTEX_LOCK(lock, op->mutex, this_ethread());
      op->handleEvent(EVENT_NONE, nullptr);
    } else if (op->thread == AIO_CALLBACK_THREAD_ANY) {
      eventProcessor.schedule_imm(op);
    } else {
      op->thread->schedule_imm(op);
    }
  }
}

#endif

int
ink_aio_read(AIOCallback *op_in, int fromAPI)
{
#if TS_USE_LINUX_IO_URING
  if (use_io_uring) {
    io_uring_prep_ops_internal(op_in, LIO_READ);
    return 1;
  }
#endif
  op_in->aiocb.aio_lio_opcode = LIO_READ;
  aio_queue_req(op_in, fromAPI);

  return 1;
}

int
ink_aio_write(AIOCallback *op_in, int fromAPI)
{
#if TS_USE_LINUX_IO_URING
  if (use_io_uring) {
    io_uring_prep_ops_internal(op_in, LIO_WRITE);
    return 1;
  }
#endif
  op_in->aiocb.aio_lio_opcode = LIO_WRITE;
  aio_queue_req(op_in, fromAPI);

  return 1;
}
