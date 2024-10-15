/** @file

  eBPF program for trylock-stats

  Based on
  https://github.com/goldshtn/linux-tracing-workshop/blob/master/lockstat-solution.py

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

#include <linux/ptrace.h>

struct thread_mutex_key_t {
  u32 tid;
  u64 mtx;
  int lock_stack_id;
};

struct thread_mutex_val_t {
  u64 wait_time_ns;
  u64 lock_time_ns;
  u64 enter_count;
  u64 fail_count; /// failure of try lock
};

struct mutex_timestamp_t {
  u64 mtx;
  u64 timestamp;
};

struct mutex_lock_time_key_t {
  u32 tid;
  u64 mtx;
};

struct mutex_lock_time_val_t {
  u64 timestamp;
  int stack_id;
};

// Mutex to the stack id which initialized that mutex
BPF_HASH(init_stacks, u64, int);
// Main info database about mutex and thread pairs
BPF_HASH(locks, struct thread_mutex_key_t, struct thread_mutex_val_t);
// PID to the mutex address and timestamp of when the wait started
BPF_HASH(lock_start, u32, struct mutex_timestamp_t);
// PID and mutex address to the timestamp of when the wait ended (mutex acquired) and the stack id
BPF_HASH(lock_end, struct mutex_lock_time_key_t, struct mutex_lock_time_val_t);
// Histogram of wait times
BPF_HISTOGRAM(mutex_wait_hist, u64);
// Histogram of hold times
BPF_HISTOGRAM(mutex_lock_hist, u64);
BPF_STACK_TRACE(stacks, 4096);

int
probe_mutex_init(struct pt_regs *ctx)
{
  int stack_id   = stacks.get_stackid(ctx, BPF_F_REUSE_STACKID | BPF_F_USER_STACK);
  u64 mutex_addr = PT_REGS_PARM1(ctx);
  init_stacks.update(&mutex_addr, &stack_id);
  return 0;
}

int
probe_mutex_lock(struct pt_regs *ctx)
{
  u64                      now = bpf_ktime_get_ns();
  u32                      pid = bpf_get_current_pid_tgid();
  struct mutex_timestamp_t val = {};
  val.mtx                      = PT_REGS_PARM1(ctx);
  val.timestamp                = now;
  lock_start.update(&pid, &val);
  return 0;
}

int
probe_mutex_lock_return(struct pt_regs *ctx)
{
  u64                       now   = bpf_ktime_get_ns();
  u32                       pid   = bpf_get_current_pid_tgid();
  struct mutex_timestamp_t *entry = lock_start.lookup(&pid);

  if (entry == NULL) {
    return 0; // Missed the entry
  }

  u64 wait_time = now - entry->timestamp;
  int stack_id  = stacks.get_stackid(ctx, BPF_F_REUSE_STACKID | BPF_F_USER_STACK);

  // If pthread_mutex_lock() returned 0, we have the lock
  if (PT_REGS_RC(ctx) == 0) {
    // Record the lock acquisition timestamp so that we can read it when unlocking
    struct mutex_lock_time_key_t key = {};
    key.mtx                          = entry->mtx;
    key.tid                          = pid;
    struct mutex_lock_time_val_t val = {};
    val.timestamp                    = now;
    val.stack_id                     = stack_id;
    lock_end.update(&key, &val);
  }

  // Record the wait time for this mutex-tid-stack combination even if locking failed
  struct thread_mutex_key_t tm_key = {};
  tm_key.mtx                       = entry->mtx;
  tm_key.tid                       = pid;
  tm_key.lock_stack_id             = stack_id;
  struct thread_mutex_val_t *existing_tm_val, new_tm_val = {};
  existing_tm_val                = locks.lookup_or_init(&tm_key, &new_tm_val);
  existing_tm_val->wait_time_ns += wait_time;

  if (PT_REGS_RC(ctx) == 0) {
    existing_tm_val->enter_count += 1;
  }

  u64 mtx_slot = bpf_log2l(wait_time / 1000);
  mutex_wait_hist.increment(mtx_slot);
  lock_start.delete(&pid);
  return 0;
}

int
probe_mutex_trylock_return(struct pt_regs *ctx)
{
  u64                       now   = bpf_ktime_get_ns();
  u32                       pid   = bpf_get_current_pid_tgid();
  struct mutex_timestamp_t *entry = lock_start.lookup(&pid);

  if (entry == NULL) {
    return 0; // Missed the entry
  }

  int stack_id = stacks.get_stackid(ctx, BPF_F_REUSE_STACKID | BPF_F_USER_STACK);

  // If pthread_mutex_lock() returned 0, we have the lock
  if (PT_REGS_RC(ctx) == 0) {
    // Record the lock acquisition timestamp so that we can read it when unlocking
    struct mutex_lock_time_key_t key = {};
    key.mtx                          = entry->mtx;
    key.tid                          = pid;
    struct mutex_lock_time_val_t val = {};
    val.timestamp                    = now;
    val.stack_id                     = stack_id;
    lock_end.update(&key, &val);
  }

  // Record the wait time for this mutex-tid-stack combination even if locking failed
  struct thread_mutex_key_t tm_key = {};
  tm_key.mtx                       = entry->mtx;
  tm_key.tid                       = pid;
  tm_key.lock_stack_id             = stack_id;
  struct thread_mutex_val_t *existing_tm_val, new_tm_val = {};
  existing_tm_val = locks.lookup_or_init(&tm_key, &new_tm_val);

  if (PT_REGS_RC(ctx) == 0) {
    existing_tm_val->enter_count += 1;
  } else {
    existing_tm_val->fail_count += 1;
  }

  lock_start.delete(&pid);
  return 0;
}

int
probe_mutex_unlock(struct pt_regs *ctx)
{
  u64                          now       = bpf_ktime_get_ns();
  u64                          mtx       = PT_REGS_PARM1(ctx);
  u32                          pid       = bpf_get_current_pid_tgid();
  struct mutex_lock_time_key_t lock_key  = {};
  lock_key.mtx                           = mtx;
  lock_key.tid                           = pid;
  struct mutex_lock_time_val_t *lock_val = lock_end.lookup(&lock_key);

  if (lock_val == NULL) {
    return 0; // Missed the lock of this mutex
  }

  u64                       hold_time        = now - lock_val->timestamp;
  struct thread_mutex_key_t tm_key           = {};
  tm_key.mtx                                 = mtx;
  tm_key.tid                                 = pid;
  tm_key.lock_stack_id                       = lock_val->stack_id;
  struct thread_mutex_val_t *existing_tm_val = locks.lookup(&tm_key);

  if (existing_tm_val == NULL) {
    return 0; // Couldn't find this record
  }

  existing_tm_val->lock_time_ns += hold_time;
  u64 slot                       = bpf_log2l(hold_time / 1000);
  mutex_lock_hist.increment(slot);
  lock_end.delete(&lock_key);
  return 0;
}
