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

#include "iocore/aio/AIO.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/net/Net.h"
#include "tscore/ink_atomic.h"
#include "tscore/ink_hw.h"
#include "tscore/Layout.h"
#include "tscore/TSSystemState.h"
#include "tscore/Random.h"
#include <iostream>
#include <fstream>

using std::cout;
using std::endl;

#include "iocore/utils/diags.i"

#define MAX_DISK_THREADS 200
#ifdef DISK_ALIGN
#define MIN_OFFSET (32 * 1024)
#else
#define MIN_OFFSET (8 * 1024)
#endif
enum {
  READ_MODE,
  WRITE_MODE,
  RANDOM_READ_MODE,
};

struct AIO_Device;
int         n_accessors = 0;
int         orig_n_accessors;
AIO_Device *dev[MAX_DISK_THREADS];

extern RecInt cache_config_threads_per_disk;

int    write_after      = 0;
int    write_skip       = 0;
int    hotset_size      = 20;
double hotset_frequency = 0.9;
int    touch_data       = 0;
int    disk_size        = 4000;
int    read_size        = 1024;
char  *disk_path[MAX_DISK_THREADS];
int    n_disk_path      = 0;
int    run_time         = 0;
int    threads_per_disk = 1;
int    delete_disks     = 0;
int    max_size         = 0;
int    use_lseek        = 0;
int    num_processors   = 0;
#if TS_USE_LINUX_IO_URING
int io_uring_queue_entries = 32;
int io_uring_sq_poll_ms    = 0;
int io_uring_attach_wq     = 0;
int io_uring_wq_bounded    = 0;
int io_uring_wq_unbounded  = 0;
int io_uring_force_thread  = 0;
#endif

int    chains                 = 1;
double seq_read_percent       = 0.0;
double seq_write_percent      = 0.0;
double rand_read_percent      = 0.0;
double real_seq_read_percent  = 0.0;
double real_seq_write_percent = 0.0;
double real_rand_read_percent = 0.0;
int    seq_read_size          = 0;
int    seq_write_size         = 0;
int    rand_read_size         = 0;

struct AIO_Device : public Continuation {
  char                        *path;
  int                          fd;
  int                          id;
  char                        *buf;
  ink_hrtime                   time_start, time_end;
  int                          seq_reads;
  int                          seq_writes;
  int                          rand_reads;
  int                          hotset_idx;
  int                          mode;
  std::unique_ptr<AIOCallback> io;
  AIO_Device(ProxyMutex *m) : Continuation(m), io{new_AIOCallback()}
  {
    hotset_idx = 0;
    time_start = 0;
    SET_HANDLER(&AIO_Device::do_hotset);
  }
  int
  select_mode(double p)
  {
    if (p < real_seq_read_percent) {
      return READ_MODE;
    } else if (p < real_seq_read_percent + real_seq_write_percent) {
      return WRITE_MODE;
    } else {
      return RANDOM_READ_MODE;
    }
  };
  void
  do_touch_data(off_t orig_len, off_t orig_offset)
  {
    if (!touch_data) {
      return;
    }
    unsigned int len    = static_cast<unsigned int>(orig_len);
    unsigned int offset = static_cast<unsigned int>(orig_offset);
    offset              = offset % 1024;
    char     *b         = buf;
    unsigned *x         = reinterpret_cast<unsigned *>(b);
    for (unsigned j = 0; j < (len / sizeof(int)); j++) {
      x[j]   = offset;
      offset = (offset + 1) % 1024;
    }
  };
  int
  do_check_data(off_t orig_len, off_t orig_offset)
  {
    if (!touch_data) {
      return 0;
    }
    unsigned int len    = static_cast<unsigned int>(orig_len);
    unsigned int offset = static_cast<unsigned int>(orig_offset);
    offset              = offset % 1024;
    unsigned *x         = reinterpret_cast<unsigned *>(buf);
    for (unsigned j = 0; j < (len / sizeof(int)); j++) {
      if (x[j] != offset) {
        return 1;
      }
      offset = (offset + 1) % 1024;
    }
    return 0;
  }
  int do_hotset(int event, Event *e);
  int do_fd(int event, Event *e);
};

void
dump_summary()
{
  /* dump timing info */
  printf("Writing summary info\n");

  printf("----------\n");
  printf("parameters\n");
  printf("----------\n");
  printf("%d disks\n", n_disk_path);
  printf("%d chains\n", chains);
  printf("%d threads_per_disk\n", threads_per_disk);

  printf("%0.1f percent %d byte seq_reads by volume\n", seq_read_percent * 100.0, seq_read_size);
  printf("%0.1f percent %d byte seq_writes by volume\n", seq_write_percent * 100.0, seq_write_size);
  printf("%0.1f percent %d byte rand_reads by volume\n", rand_read_percent * 100.0, rand_read_size);
  printf("-------\n");
  printf("factors\n");
  printf("-------\n");
  printf("%0.1f percent %d byte seq_reads by count\n", real_seq_read_percent * 100.0, seq_read_size);
  printf("%0.1f percent %d byte seq_writes by count\n", real_seq_write_percent * 100.0, seq_write_size);
  printf("%0.1f percent %d byte rand_reads by count\n", real_rand_read_percent * 100.0, rand_read_size);

  printf("-------------------------\n");
  printf("individual thread results\n");
  printf("-------------------------\n");
  double total_seq_reads  = 0;
  double total_seq_writes = 0;
  double total_rand_reads = 0;
  double total_secs       = 0.0;
  for (int i = 0; i < orig_n_accessors; i++) {
    double secs    = (dev[i]->time_end - dev[i]->time_start) / 1000000000.0;
    double ops_sec = (dev[i]->seq_reads + dev[i]->seq_writes + dev[i]->rand_reads) / secs;
    printf("%s: #sr:%d #sw:%d #rr:%d %0.1f secs %0.1f ops/sec\n", dev[i]->path, dev[i]->seq_reads, dev[i]->seq_writes,
           dev[i]->rand_reads, secs, ops_sec);
    total_secs       += secs;
    total_seq_reads  += dev[i]->seq_reads;
    total_seq_writes += dev[i]->seq_writes;
    total_rand_reads += dev[i]->rand_reads;
  }
  printf("-----------------\n");
  printf("aggregate results\n");
  printf("-----------------\n");
  total_secs /= orig_n_accessors;
  float sr    = (total_seq_reads * seq_read_size) / total_secs;
  sr         /= 1024.0 * 1024.0;
  float sw    = (total_seq_writes * seq_write_size) / total_secs;
  sw         /= 1024.0 * 1024.0;
  float rr    = (total_rand_reads * rand_read_size) / total_secs;
  rr         /= 1024.0 * 1024.0;
  printf("%f ops %0.2f mbytes/sec %0.1f ops/sec %0.1f ops/sec/disk seq_read\n", total_seq_reads, sr, total_seq_reads / total_secs,
         total_seq_reads / total_secs / n_disk_path);
  printf("%f ops %0.2f mbytes/sec %0.1f ops/sec %0.1f ops/sec/disk seq_write\n", total_seq_writes, sw,
         total_seq_writes / total_secs, total_seq_writes / total_secs / n_disk_path);
  printf("%f ops %0.2f mbytes/sec %0.1f ops/sec %0.1f ops/sec/disk rand_read\n", total_rand_reads, rr,
         total_rand_reads / total_secs, total_rand_reads / total_secs / n_disk_path);
  printf("%0.2f total mbytes/sec\n", sr + sw + rr);
  printf("----------------------------------------------------------\n");

#if TS_USE_LINUX_IO_URING
  printf("-----------------\n");
  printf("IO_URING results\n");
  printf("-----------------\n");

  auto completed = ts::Metrics::Counter::lookup("proxy.process.io_uring.completed", nullptr);
  auto submitted = ts::Metrics::Counter::lookup("proxy.process.io_uring.submitted", nullptr);

  printf("submissions: %lu\n", ts::Metrics::Counter::load(submitted));
  printf("completions: %lu\n", ts::Metrics::Counter::load(completed));
#endif

  if (delete_disks) {
    for (int i = 0; i < n_disk_path; i++) {
      unlink(disk_path[i]);
    }
  }
  exit(0);
}

int
AIO_Device::do_hotset(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  off_t max_offset         = (static_cast<off_t>(disk_size)) * 1024 * 1024;
  io->aiocb.aio_lio_opcode = LIO_WRITE;
  io->aiocb.aio_fildes     = fd;
  io->aiocb.aio_offset     = MIN_OFFSET + hotset_idx * max_size;
  do_touch_data(seq_read_size, io->aiocb.aio_offset);
  ink_assert(!do_check_data(seq_read_size, io->aiocb.aio_offset));
  if (!hotset_idx) {
    fprintf(stderr, "Starting hotset document writing \n");
  }
  if (io->aiocb.aio_offset > max_offset) {
    fprintf(stderr, "Finished hotset documents  [%d] offset [%6.0f] size [%6.0f]\n", hotset_idx, static_cast<float> MIN_OFFSET,
            static_cast<float>(max_size));
    SET_HANDLER(&AIO_Device::do_fd);
    eventProcessor.schedule_imm(this);
    return (0);
  }
  io->aiocb.aio_nbytes = seq_read_size;
  io->aiocb.aio_buf    = buf;
  io->action           = this;
  io->thread           = mutex->thread_holding;
  ink_assert(ink_aio_write(io.get()) >= 0);
  hotset_idx++;
  return 0;
}

int
AIO_Device::do_fd(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (!time_start) {
    time_start = ink_get_hrtime();
    fprintf(stderr, "Starting the aio_testing \n");
  }
  if ((ink_get_hrtime() - time_start) > (run_time * HRTIME_SECOND)) {
    time_end = ink_get_hrtime();
    ink_atomic_increment(&n_accessors, -1);
    if (n_accessors <= 0) {
      dump_summary();
    }
    return 0;
  }

  off_t max_offset         = (static_cast<off_t>(disk_size)) * 1024 * 1024;   // MB-GB
  off_t max_hotset_offset  = (static_cast<off_t>(hotset_size)) * 1024 * 1024; // MB-GB
  off_t seq_read_point     = (static_cast<off_t> MIN_OFFSET);
  off_t seq_write_point    = (static_cast<off_t> MIN_OFFSET) + max_offset / 2 + write_after * 1024 * 1024;
  seq_write_point         += (id % n_disk_path) * (max_offset / (threads_per_disk * 4));
  if (seq_write_point > max_offset) {
    seq_write_point = MIN_OFFSET;
  }

  if (io->aiocb.aio_lio_opcode == LIO_READ) {
    ink_assert(!do_check_data(io->aiocb.aio_nbytes, io->aiocb.aio_offset));
  }
  memset(buf, 0, max_size);
  io->aiocb.aio_fildes = fd;
  io->aiocb.aio_buf    = buf;
  io->action           = this;
  io->thread           = mutex->thread_holding;

  switch (select_mode(ts::Random::drandom())) {
  case READ_MODE:
    io->aiocb.aio_offset     = seq_read_point;
    io->aiocb.aio_nbytes     = seq_read_size;
    io->aiocb.aio_lio_opcode = LIO_READ;
    ink_assert(ink_aio_read(io.get()) >= 0);
    seq_read_point += seq_read_size;
    if (seq_read_point > max_offset) {
      seq_read_point = MIN_OFFSET;
    }
    seq_reads++;
    break;
  case WRITE_MODE:
    io->aiocb.aio_offset     = seq_write_point;
    io->aiocb.aio_nbytes     = seq_write_size;
    io->aiocb.aio_lio_opcode = LIO_WRITE;
    do_touch_data(seq_write_size, (static_cast<int>(seq_write_point)) % 1024);
    ink_assert(ink_aio_write(io.get()) >= 0);
    seq_write_point += seq_write_size;
    seq_write_point += write_skip;
    if (seq_write_point > max_offset) {
      seq_write_point = MIN_OFFSET;
    }

    seq_writes++;
    break;
  case RANDOM_READ_MODE: {
    // fprintf(stderr, "random read started \n");
    double p, f;
    p       = ts::Random::drandom();
    f       = ts::Random::drandom();
    off_t o = 0;
    if (f < hotset_frequency) {
      o = static_cast<off_t>(p) * max_hotset_offset;
    } else {
      o = static_cast<off_t>(p) * (max_offset - rand_read_size);
    }
    if (o < MIN_OFFSET) {
      o = MIN_OFFSET;
    }
    o                        = (o + (seq_read_size - 1)) & (~(seq_read_size - 1));
    io->aiocb.aio_offset     = o;
    io->aiocb.aio_nbytes     = rand_read_size;
    io->aiocb.aio_lio_opcode = LIO_READ;
    ink_assert(ink_aio_read(io.get()) >= 0);
    rand_reads++;
    break;
  }
  }
  return 0;
}

#define PARAM(_s)                               \
  else if (strcmp(field_name, #_s) == 0)        \
  {                                             \
    fin >> _s;                                  \
    cout << "reading " #_s " = " << _s << endl; \
  }

int
read_config(const char *config_filename)
{
  std::ifstream fin(config_filename);
  if (!fin.rdbuf()->is_open()) {
    return (0);
  }

  char field_name[256];
  char field_value[256];

  while (!fin.eof()) {
    field_name[0] = '\0';
    fin >> field_name;
    if (false) {}
    PARAM(hotset_size)
    PARAM(hotset_frequency)
    PARAM(touch_data)
    PARAM(use_lseek)
    PARAM(write_after)
    PARAM(write_skip)
    PARAM(disk_size)
    PARAM(seq_read_percent)
    PARAM(seq_write_percent)
    PARAM(rand_read_percent)
    PARAM(seq_read_size)
    PARAM(seq_write_size)
    PARAM(rand_read_size)
    PARAM(run_time)
    PARAM(chains)
    PARAM(threads_per_disk)
    PARAM(delete_disks)
    PARAM(num_processors)
#if TS_USE_LINUX_IO_URING
    PARAM(io_uring_queue_entries)
    PARAM(io_uring_sq_poll_ms)
    PARAM(io_uring_attach_wq)
    PARAM(io_uring_wq_bounded)
    PARAM(io_uring_wq_unbounded)
    PARAM(io_uring_force_thread)
#endif
    else if (strcmp(field_name, "disk_path") == 0)
    {
      ink_assert(n_disk_path < MAX_DISK_THREADS);
      fin >> field_value;
      disk_path[n_disk_path] = strdup(field_value);
      cout << "reading disk_path = " << disk_path[n_disk_path] << endl;
      n_disk_path++;
    }
  }
  ink_assert(read_size > 0);
  int t                  = seq_read_size + seq_write_size + rand_read_size;
  real_seq_read_percent  = seq_read_percent;
  real_seq_write_percent = seq_write_percent;
  real_rand_read_percent = rand_read_percent;
  if (seq_read_size) {
    real_seq_read_percent *= t / seq_read_size;
  }
  if (seq_write_size) {
    real_seq_write_percent *= t / seq_write_size;
  }
  if (rand_read_size) {
    real_rand_read_percent *= t / rand_read_size;
  }
  float tt               = real_seq_read_percent + real_seq_write_percent + real_rand_read_percent;
  real_seq_read_percent  = real_seq_read_percent / tt;
  real_seq_write_percent = real_seq_write_percent / tt;
  real_rand_read_percent = real_rand_read_percent / tt;
  return (1);
}

#if TS_USE_LINUX_IO_URING

class IOUringLoopTailHandler : public EThread::LoopTailHandler
{
public:
  int
  waitForActivity(ink_hrtime timeout) override
  {
    IOUringContext::local_context()->submit_and_wait(timeout);

    return 0;
  }
  /** Unblock.

  This is required to unblock (wake up) the block created by calling @a cb.
      */
  void
  signalActivity() override
  {
  }

  ~IOUringLoopTailHandler() override {}
} uring_handler;

#endif

int
main(int argc, char *argv[])
{
  int i;

  // Read the configuration file
  const char *config_filename = "sample.cfg";
  if (argc == 2) {
    config_filename = argv[1];
  }
  printf("configuration file: %s\n", config_filename);
  if (!read_config(config_filename)) {
    exit(1);
  }

  if (num_processors == 0) {
    num_processors = ink_number_of_processors();
  }
  printf("Using %d processor threads\n", num_processors);

  AIOBackend backend = AIOBackend::AIO_BACKEND_AUTO;

#if TS_USE_LINUX_IO_URING
  {
    IOUringConfig cfg;

    cfg.queue_entries = io_uring_queue_entries;
    cfg.sq_poll_ms    = io_uring_sq_poll_ms;
    cfg.attach_wq     = io_uring_attach_wq;
    cfg.wq_bounded    = io_uring_wq_bounded;
    cfg.wq_unbounded  = io_uring_wq_unbounded;

    IOUringContext::set_config(cfg);

    if (io_uring_force_thread) {
      backend = AIOBackend::AIO_BACKEND_THREAD;
    }
  };
#endif

  Layout::create();
  init_diags("", nullptr);
  RecProcessInit();
  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(num_processors);

  Thread *main_thread = new EThread;
  main_thread->set_specific();

#if TS_USE_LINUX_IO_URING
  if (!io_uring_force_thread) {
    for (EThread *et : eventProcessor.active_group_threads(ET_NET)) {
      et->set_tail_handler(&uring_handler);
    }
  }
#endif

  RecProcessStart();
  ink_aio_init(AIO_MODULE_PUBLIC_VERSION, backend);
  ts::Random::seed(time(nullptr));

  max_size = seq_read_size;
  if (seq_write_size > max_size) {
    max_size = seq_write_size;
  }
  if (rand_read_size > max_size) {
    max_size = rand_read_size;
  }

  cache_config_threads_per_disk = threads_per_disk;
  orig_n_accessors              = n_disk_path * threads_per_disk;

  for (i = 0; i < n_disk_path; i++) {
    for (int j = 0; j < threads_per_disk; j++) {
      dev[n_accessors]             = new AIO_Device(new_ProxyMutex());
      dev[n_accessors]->id         = i * threads_per_disk + j;
      dev[n_accessors]->path       = disk_path[i];
      dev[n_accessors]->seq_reads  = 0;
      dev[n_accessors]->seq_writes = 0;
      dev[n_accessors]->rand_reads = 0;
      dev[n_accessors]->fd         = open(dev[n_accessors]->path, O_RDWR | O_CREAT, 0600);
      fchmod(dev[n_accessors]->fd, S_IRWXU | S_IRWXG);
      if (dev[n_accessors]->fd < 0) {
        perror(disk_path[i]);
        exit(1);
      }
      dev[n_accessors]->buf = static_cast<char *>(valloc(max_size));
      eventProcessor.schedule_imm(dev[n_accessors]);
      n_accessors++;
    }
  }

  while (!TSSystemState::is_event_system_shut_down()) {
#if TS_USE_LINUX_IO_URING
    IOUringContext::local_context()->submit_and_wait(1 * HRTIME_SECOND);
#else
    sleep(1);
#endif
  }

  delete main_thread;
}
