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

/****************************************************************************

  StatSystem.h --
  Created On          : Fri Apr 3 19:41:39 1998
 ****************************************************************************/
#if !defined(_StatSystem_h_)
#define _StatSystem_h_

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_hrtime.h"
#include "ts/ink_atomic.h"

#ifdef USE_LOCKS_FOR_DYN_STATS
#include "Lock.h"
#endif

#include "ts/ink_apidefs.h"
#include "ts/apidefs.h"

#define STATS_MAJOR_VERSION 6 // increment when changing the stats!
#define DEFAULT_SNAP_FILENAME "stats.snap"

/////////////////////////////////////////////////////////////
//
// class TransactionMilestones
//
/////////////////////////////////////////////////////////////
class TransactionMilestones
{
public:
  TransactionMilestones() { ink_zero(milestones); }

  ink_hrtime &operator[](TSMilestonesType ms) { return milestones[ms]; }

  ink_hrtime operator[](TSMilestonesType ms) const { return milestones[ms]; }

  /**
   * Takes two milestones and returns the difference.
   * @param start The start time
   * @param end The end time
   * @return A double that is the time in seconds
   */
  int64_t
  difference_msec(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    if (milestones[ms_end] == 0) {
      return -1;
    }
    return (milestones[ms_end] - milestones[ms_start]) / 1000000;
  }

  double
  difference(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    if (milestones[ms_end] == 0) {
      return -1;
    }
    return (double)(milestones[ms_end] - milestones[ms_start]) / 1000000000;
  }

  ink_hrtime
  elapsed(TSMilestonesType ms_start, TSMilestonesType ms_end) const
  {
    return milestones[ms_end] - milestones[ms_start];
  }

private:
  ink_hrtime milestones[TS_MILESTONE_LAST_ENTRY];
};

// Modularization Project: Build w/o thread-local-dyn-stats
// temporarily until we switch over to librecords.  Revert to old
// non-thread-local system so that TS will still build and run.

//---------------------------------------------------------------------//
//                       Welcome to enum land!                         //
//---------------------------------------------------------------------//

// Before adding a stat variable, decide whether it is of
// a "transaction" type or if it is of a "dynamic" type.
// Then add the stat variable to the appropriate enumeration
// type. Make sure that DYN_STAT_START is large enough
// (read comment below).

//
// Http Transaction Stats
//
#define _HEADER typedef enum { NO_HTTP_TRANS_STATS = 0,

#define _FOOTER        \
  MAX_HTTP_TRANS_STATS \
  }                    \
  HttpTransactionStat_t;

#if defined(freebsd)
#undef _D
#endif
#define _D(_x) _x,

#include "HttpTransStats.h"
#undef _HEADER
#undef _FOOTER
#undef _D

struct HttpTransactionStatsString_t {
  HttpTransactionStat_t i;
  char *name;
};

//
// Note: DYN_STAT_START needs to be at least the next
// power of 2 bigger than the value of MAX_HTTP_TRANS_STATS
//
#define DYN_STAT_START 2048
#define DYN_STAT_MASK (~(2047UL))
//
// Dynamic Stats
//
#define _HEADER typedef enum { NO_DYN_STATS = DYN_STAT_START,

#define _FOOTER \
  MAX_DYN_STATS \
  }             \
  DynamicStat_t;

#define _D(_x) _x,

#include "DynamicStats.h"

#undef _HEADER
#undef _FOOTER
#undef _D

struct DynamicStatsString_t {
  DynamicStat_t i;
  const char *name;
};

extern HttpTransactionStatsString_t HttpTransactionStatsStrings[];
extern DynamicStatsString_t DynamicStatsStrings[];

//---------------------------------------------------------------------//
//                          Typedefs, etc.                             //
//---------------------------------------------------------------------//

// For now, use mutexes. May later change to spin_locks, try_locks.
#define ink_stat_lock_t ink_mutex

typedef int64_t ink_statval_t;

struct ink_local_stat_t {
  ink_statval_t count;
  ink_statval_t value;
};

struct ink_prot_global_stat_t {
  ink_stat_lock_t access_lock;
  ink_statval_t count;
  ink_statval_t sum;

  ink_prot_global_stat_t() : count(0), sum(0) { ink_mutex_init(&access_lock, "Stats Access Lock"); }
};

struct ink_unprot_global_stat_t {
  ink_statval_t count;
  ink_statval_t sum;
  ink_unprot_global_stat_t() : count(0), sum(0) {}
};


//---------------------------------------------------------------------//
//                     External interface macros                       //
//---------------------------------------------------------------------//

// Set count and sum to 0.
#define CLEAR_DYN_STAT(X)                      \
  {                                            \
    ink_assert(X &DYN_STAT_MASK);              \
    CLEAR_GLOBAL_DYN_STAT(X - DYN_STAT_START); \
  }

#define DECREMENT_DYN_STAT(X) SUM_DYN_STAT(X, (ink_statval_t)-1)

#define COUNT_DYN_STAT(X, C)                          \
  {                                                   \
    ink_assert(X &DYN_STAT_MASK);                     \
    ADD_TO_GLOBAL_DYN_COUNT((X - DYN_STAT_START), C); \
  }

#define FSUM_DYN_STAT(X, S)                          \
  {                                                  \
    ink_assert(X &DYN_STAT_MASK);                    \
    ADD_TO_GLOBAL_DYN_FSUM((X - DYN_STAT_START), S); \
  }

// Increment the count, sum.
#define INCREMENT_DYN_STAT(X) SUM_DYN_STAT(X, (ink_statval_t)1)

// Get the count and sum in a single lock acquire operation.
// Would it make sense to have three functions - a combined
// read of the count and sum, and two more functions - one
// to read just the count and the other to read just the sum?
#define READ_DYN_STAT(X, C, S)                        \
  {                                                   \
    ink_assert(X &DYN_STAT_MASK);                     \
    READ_GLOBAL_DYN_STAT((X - DYN_STAT_START), C, S); \
  }

#define READ_DYN_COUNT(X, C)                        \
  {                                                 \
    ink_assert(X &DYN_STAT_MASK);                   \
    READ_GLOBAL_DYN_COUNT((X - DYN_STAT_START), C); \
  }

#define READ_DYN_SUM(X, S)                        \
  {                                               \
    ink_assert(X &DYN_STAT_MASK);                 \
    READ_GLOBAL_DYN_SUM((X - DYN_STAT_START), S); \
  }

// set the stat.count to a specific value
#define SET_DYN_COUNT(X, V)                        \
  {                                                \
    ink_assert(X &DYN_STAT_MASK);                  \
    SET_GLOBAL_DYN_COUNT((X - DYN_STAT_START), V); \
  }

// set the stat.count stat.sum to specific values
#define SET_DYN_STAT(X, C, S)                        \
  {                                                  \
    ink_assert(X &DYN_STAT_MASK);                    \
    SET_GLOBAL_DYN_STAT((X - DYN_STAT_START), C, S); \
  }

// Add a specific value to the sum.
#define SUM_DYN_STAT(X, S)                          \
  {                                                 \
    ink_assert(X &DYN_STAT_MASK);                   \
    ADD_TO_GLOBAL_DYN_SUM((X - DYN_STAT_START), S); \
  }

// Add a specific value to the sum.
#define SUM_GLOBAL_DYN_STAT(X, S)                          \
  {                                                        \
    ink_assert(X &DYN_STAT_MASK);                          \
    ADD_TO_GLOBAL_GLOBAL_DYN_SUM((X - DYN_STAT_START), S); \
  }

#define __CLEAR_TRANS_STAT(local_stat_struct_, X)   \
  {                                                 \
    ink_assert(!(X & DYN_STAT_MASK));               \
    local_stat_struct_[X].count = (ink_statval_t)0; \
    local_stat_struct_[X].value = (ink_statval_t)0; \
  }

#define __DECREMENT_TRANS_STAT(local_stat_struct_, X) __SUM_TRANS_STAT(local_stat_struct_, X, (ink_statval_t)-1)

#define __FSUM_TRANS_STAT(local_stat_struct_, X, S) \
  {                                                 \
    ink_assert(!(X & DYN_STAT_MASK));               \
    local_stat_struct_[X].count++;                  \
    (*(double *)&local_stat_struct_[X].value) += S; \
  }

// Increment the count, sum.
#define __INCREMENT_TRANS_STAT(local_stat_struct_, X) __SUM_TRANS_STAT(local_stat_struct_, X, (ink_statval_t)1);

#define __INITIALIZE_LOCAL_STAT_STRUCT(local_stat_struct_, X) __CLEAR_TRANS_STAT(local_stat_struct_, X)

#define INITIALIZE_GLOBAL_TRANS_STATS(X) \
  {                                      \
    X.count = (ink_statval_t)0;          \
    X.sum = (ink_statval_t)0;            \
  }

// Get the count and sum in a single lock acquire operation.
// Would it make sense to have three functions - a combined
// read of the count and sum, and two more functions - one
// to read just the count and the other to read just the sum?
#define READ_HTTP_TRANS_STAT(X, C, S)     \
  {                                       \
    ink_assert(!(X & DYN_STAT_MASK));     \
    READ_GLOBAL_HTTP_TRANS_STAT(X, C, S); \
  }

// set the stat.count to a specific value
#define __SET_TRANS_COUNT(local_stat_struct_, X, V) \
  {                                                 \
    ink_assert(!(X & DYN_STAT_MASK));               \
    local_stat_struct_[X].value = (ink_statval_t)V; \
  }

// set the stat.count and the stat.sum to specific values
#define __SET_TRANS_STAT(local_stat_struct_, X, C, S) \
  {                                                   \
    ink_assert(!(X & DYN_STAT_MASK));                 \
    local_stat_struct_[X].value = (ink_statval_t)S;   \
  }

// Add a specific value to local stat.
// Both ADD_TO_SUM_STAT and ADD_TO_COUNT_STAT do the same thing
// to the local copy of the transaction stat.
#define __SUM_TRANS_STAT(local_stat_struct_, X, S) \
  {                                                \
    ink_assert(!(X & DYN_STAT_MASK));              \
    local_stat_struct_[X].count += 1;              \
    local_stat_struct_[X].value += S;              \
  }

#define UPDATE_HTTP_TRANS_STATS(local_stat_struct_)                    \
  {                                                                    \
    int i;                                                             \
    STAT_LOCK_ACQUIRE(&(global_http_trans_stat_lock));                 \
    for (i = NO_HTTP_TRANS_STATS; i < MAX_HTTP_TRANS_STATS; i++) {     \
      global_http_trans_stats[i].count += local_stat_struct_[i].count; \
      global_http_trans_stats[i].sum += local_stat_struct_[i].value;   \
    }                                                                  \
    STAT_LOCK_RELEASE(&(global_http_trans_stat_lock));                 \
  }

#define STAT_LOCK_ACQUIRE(X) (ink_mutex_acquire(X))
#define STAT_LOCK_RELEASE(X) (ink_mutex_release(X))
#define STAT_LOCK_INIT(X, S) (ink_mutex_init(X, S))

//---------------------------------------------------------------------//
// Internal macros to support adding, setting, reading, clearing, etc. //
//---------------------------------------------------------------------//

#ifndef USE_LOCKS_FOR_DYN_STATS

#ifdef USE_THREAD_LOCAL_DYN_STATS
// Modularization Project: See note above
#error "Should not build with USE_THREAD_LOCAL_DYN_STATS"


#define ADD_TO_GLOBAL_DYN_COUNT(X, C) mutex->thread_holding->global_dyn_stats[X].count += (C)

#define ADD_TO_GLOBAL_DYN_SUM(X, S)                   \
  mutex->thread_holding->global_dyn_stats[X].count++; \
  mutex->thread_holding->global_dyn_stats[X].sum += (S)

#define ADD_TO_GLOBAL_GLOBAL_DYN_SUM(X, S)                            \
  ink_atomic_increment(&global_dyn_stats[X].count, (ink_statval_t)1); \
  ink_atomic_increment(&global_dyn_stats[X].sum, S)
/*
 * global_dyn_stats[X].count ++; \
 * global_dyn_stats[X].sum += (S)
 */

#define ADD_TO_GLOBAL_DYN_FSUM(X, S)                  \
  mutex->thread_holding->global_dyn_stats[X].count++; \
  mutex->thread_holding->global_dyn_stats[X].sum += (S)

#define CLEAR_GLOBAL_DYN_STAT(X) \
  global_dyn_stats[X].count = 0; \
  global_dyn_stats[X].sum = 0

#define READ_GLOBAL_DYN_STAT(X, C, S)                                         \
  do {                                                                        \
    ink_unprot_global_stat_t _s = global_dyn_stats[X];                        \
    for (int _e = 0; _e < eventProcessor.n_ethreads; _e++) {                  \
      _s.count += eventProcessor.all_ethreads[_e]->global_dyn_stats[X].count; \
      _s.sum += eventProcessor.all_ethreads[_e]->global_dyn_stats[X].sum;     \
    }                                                                         \
    for (int _e = 0; _e < eventProcessor.n_dthreads; _e++) {                  \
      _s.count += eventProcessor.all_dthreads[_e]->global_dyn_stats[X].count; \
      _s.sum += eventProcessor.all_dthreads[_e]->global_dyn_stats[X].sum;     \
    }                                                                         \
    C = _s.count;                                                             \
    S = _s.sum;                                                               \
  } while (0)

#define READ_GLOBAL_DYN_COUNT(X, C)                                     \
  do {                                                                  \
    ink_statval_t _s = global_dyn_stats[X].count;                       \
    for (int _e = 0; _e < eventProcessor.n_ethreads; _e++)              \
      _s += eventProcessor.all_ethreads[_e]->global_dyn_stats[X].count; \
    for (int _e = 0; _e < eventProcessor.n_dthreads; _e++)              \
      _s += eventProcessor.all_dthreads[_e]->global_dyn_stats[X].count; \
    C = _s;                                                             \
  } while (0)

#define READ_GLOBAL_DYN_SUM(X, S)                                     \
  do {                                                                \
    ink_statval_t _s = global_dyn_stats[X].sum;                       \
    for (int _e = 0; _e < eventProcessor.n_ethreads; _e++)            \
      _s += eventProcessor.all_ethreads[_e]->global_dyn_stats[X].sum; \
    for (int _e = 0; _e < eventProcessor.n_dthreads; _e++)            \
      _s += eventProcessor.all_dthreads[_e]->global_dyn_stats[X].sum; \
    S = _s;                                                           \
  } while (0)

#define READ_GLOBAL_HTTP_TRANS_STAT(X, C, S) \
  {                                          \
    C = global_http_trans_stats[X].count;    \
    S = global_http_trans_stats[X].sum;      \
  }

#define SET_GLOBAL_DYN_COUNT(X, V) global_dyn_stats[X].count = V

#define SET_GLOBAL_DYN_STAT(X, C, S) \
  global_dyn_stats[X].count = C;     \
  global_dyn_stats[X].sum = S

#define INITIALIZE_GLOBAL_DYN_STATS(X, T) \
  {                                       \
    X.count = (ink_statval_t)0;           \
    X.sum = (ink_statval_t)0;             \
  }

#else

#define ADD_TO_GLOBAL_DYN_COUNT(X, C) ink_atomic_increment(&global_dyn_stats[X].count, C)

#define ADD_TO_GLOBAL_DYN_SUM(X, S)                                   \
  ink_atomic_increment(&global_dyn_stats[X].count, (ink_statval_t)1); \
  ink_atomic_increment(&global_dyn_stats[X].sum, S)

#define ADD_TO_GLOBAL_GLOBAL_DYN_SUM(X, S)                            \
  ink_atomic_increment(&global_dyn_stats[X].count, (ink_statval_t)1); \
  ink_atomic_increment(&global_dyn_stats[X].sum, S)

#define ADD_TO_GLOBAL_DYN_FSUM(X, S)                                  \
  ink_atomic_increment(&global_dyn_stats[X].count, (ink_statval_t)1); \
  (*(double *)&global_dyn_stats[X].sum) += S

#define CLEAR_GLOBAL_DYN_STAT(X) \
  global_dyn_stats[X].count = 0; \
  global_dyn_stats[X].sum = 0

#define READ_GLOBAL_DYN_STAT(X, C, S) \
  C = global_dyn_stats[X].count;      \
  S = global_dyn_stats[X].sum

#define READ_GLOBAL_DYN_COUNT(X, C) C = global_dyn_stats[X].count;

#define READ_GLOBAL_DYN_SUM(X, S) S = global_dyn_stats[X].sum;

#define READ_GLOBAL_HTTP_TRANS_STAT(X, C, S) \
  {                                          \
    C = global_http_trans_stats[X].count;    \
    S = global_http_trans_stats[X].sum;      \
  }

#define SET_GLOBAL_DYN_COUNT(X, V) global_dyn_stats[X].count = V

#define SET_GLOBAL_DYN_STAT(X, C, S) \
  global_dyn_stats[X].count = C;     \
  global_dyn_stats[X].sum = S

#define INITIALIZE_GLOBAL_DYN_STATS(X, T) \
  {                                       \
    X.count = (ink_statval_t)0;           \
    X.sum = (ink_statval_t)0;             \
  }

#endif /* USE_THREAD_LOCAL_DYN_STATS */

#else /* USE_LOCKS_FOR_DYN_STATS */

#define ADD_TO_GLOBAL_DYN_COUNT(X, C)                      \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count += C;                        \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define ADD_TO_GLOBAL_DYN_SUM(X, S)                        \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count += 1;                        \
    global_dyn_stats[X].sum += S;                          \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define ADD_TO_GLOBAL_GLOBAL_DYN_SUM(X, S)                 \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count += 1;                        \
    global_dyn_stats[X].sum += S;                          \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define ADD_TO_GLOBAL_DYN_FSUM(X, S)                       \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count += (ink_statval_t)1;         \
    (*(double *)&global_dyn_stats[X].sum) += S;            \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define CLEAR_GLOBAL_DYN_STAT(X)                           \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count = (ink_statval_t)0;          \
    global_dyn_stats[X].sum = (ink_statval_t)0;            \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define READ_GLOBAL_DYN_STAT(X, C, S)                      \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    C = global_dyn_stats[X].count;                         \
    S = global_dyn_stats[X].sum;                           \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }
#define READ_GLOBAL_HTTP_TRANS_STAT(X, C, S) \
  {                                          \
    C = global_http_trans_stats[X].count;    \
    S = global_http_trans_stats[X].sum;      \
  }
#define SET_GLOBAL_DYN_COUNT(X, V)                         \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count = V;                         \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }

#define SET_GLOBAL_DYN_STAT(X, C, S)                       \
  {                                                        \
    STAT_LOCK_ACQUIRE(&(global_dyn_stats[X].access_lock)); \
    global_dyn_stats[X].count = C;                         \
    global_dyn_stats[X].sum = S;                           \
    STAT_LOCK_RELEASE(&(global_dyn_stats[X].access_lock)); \
  }

#define INITIALIZE_GLOBAL_DYN_STATS(X, T) \
  {                                       \
    STAT_LOCK_INIT(&(X.access_lock), T);  \
    X.count = (ink_statval_t)0;           \
    X.sum = (ink_statval_t)0;             \
  }

#endif /* USE_LOCKS_FOR_DYN_STATS */

//---------------------------------------------------------------------//
//                        Function prototypes                          //
//---------------------------------------------------------------------//
extern void start_stats_snap(void);
void initialize_all_global_stats();

//---------------------------------------------------------------------//
//                 Global variables declaration.                       //
//---------------------------------------------------------------------//
extern ink_stat_lock_t global_http_trans_stat_lock;
extern ink_unprot_global_stat_t global_http_trans_stats[MAX_HTTP_TRANS_STATS];
#ifndef USE_LOCKS_FOR_DYN_STATS
extern inkcoreapi ink_unprot_global_stat_t global_dyn_stats[MAX_DYN_STATS - DYN_STAT_START];
#else
extern inkcoreapi ink_prot_global_stat_t global_dyn_stats[MAX_DYN_STATS - DYN_STAT_START];
#endif

#ifdef DEBUG
extern ink_mutex http_time_lock;
extern time_t last_http_local_time;
#endif

#define MAX_HTTP_HANDLER_EVENTS 25
extern void clear_http_handler_times();
extern void print_http_handler_time(int event);
extern void print_all_http_handler_times();
#ifdef DEBUG
extern ink_hrtime http_handler_times[MAX_HTTP_HANDLER_EVENTS];
extern int http_handler_counts[MAX_HTTP_HANDLER_EVENTS];
#endif


#endif /* _StatSystem_h_ */
