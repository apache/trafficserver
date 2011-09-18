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

/***************************************************************************
 CoupledStats
 ***************************************************************************/

#ifndef COUPLED_STATS_H
#define COUPLED_STATS_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>

#include "ink_resource.h"
#include "ink_assert.h"
#include "ProxyConfig.h"
#include "Stats.h"

#if defined(_c_impl) && defined(__GNUC__) // TODO: review and simplify
#define CST_INLINE
#else
#define CST_INLINE inline
#endif

class CoupledStats;
class CoupledStatsSnapshot
{
public:
  CoupledStats * m_src;
  StatDescriptor *m_stats;
  int m_stat_count;
  int m_fetch;

  void CommitUpdates();
  StatDescriptor *fetch(const char *name);
  StatDescriptor *fetchNext();
    CoupledStatsSnapshot(CoupledStats * src);
   ~CoupledStatsSnapshot();
};


class CoupledStats
{
private:
  friend class CoupledStatsSnapshot;

  enum
  { GROW_SIZE = 5 };
  StatDescriptor **m_stats;
  int m_stat_count;
  int m_max_count;
  ink_mutex m_mux;
  bool m_snap_taken;
  char m_name[80];

  void grow_check(StatDescriptor * val);

  // dont delete these!
   ~CoupledStats()
  {
    if (m_stats) {
      delete[]m_stats;
    }
  }

public:
    CoupledStats(const char *name);

  StatDescriptor *CreateStat(const char *name, int64_t init_val);

  StatDescriptor *CreateStat(const char *name, float init_val);
};

CST_INLINE CoupledStats::CoupledStats(const char *name):
m_stat_count(-1),
m_max_count(0),
m_snap_taken(false)
{

  if (!name || strlen(name) >= 80) {
    name = "nil_category";
  }
  ink_strlcpy(m_name, name, sizeof(m_name));

  ink_mutex_init(&m_mux, "CoupledStatMutex");
  grow_check(NULL);
}

CST_INLINE void
CoupledStats::grow_check(StatDescriptor * val)
{
  if (m_stat_count == (m_max_count - 1)) {
    int cnt = m_max_count;
    StatDescriptor **old = m_stats;

    m_stats = NEW(new StatDescriptor *[m_max_count + GROW_SIZE]);
    memset(&m_stats[cnt], 0, GROW_SIZE);

    if (cnt) {
      memcpy(&m_stats[0], &old[0], cnt);
      delete[]old;
    }

    m_max_count += GROW_SIZE;
  }

  if (val != NULL) {
    m_stats[m_stat_count++] = val;
  } else {
    m_stat_count = 0;
  }
}

CST_INLINE StatDescriptor *
CoupledStats::CreateStat(const char *name, int64_t init_val)
{
  StatDescriptor *ret = NULL;

  if (m_snap_taken) {
    Warning("Attempt to create coupled stat after " "creating snapshot, request discarded");
    return NULL;
  }
  //FIXME: old sdk didnt use category, we _should_ but how to config ...
  //else if (ret = StatDescriptor::CreateDescriptor(m_name, name, init_val))
  else if ((ret = StatDescriptor::CreateDescriptor(name, init_val))) {
    ink_mutex_acquire(&m_mux);
    grow_check(ret);
    ink_mutex_release(&m_mux);
  }

  return ret;
}

CST_INLINE StatDescriptor *
CoupledStats::CreateStat(const char *name, float init_val)
{
  StatDescriptor *ret = NULL;

  if (m_snap_taken) {
    Warning("Attempt to create coupled stat after " "creating snapshot, request discarded");
    return NULL;
  }
  //FIXME: old sdk didnt use category, we _should_ but how to config ...
  //else if (ret = StatDescriptor::CreateDescriptor(m_name, name, init_val))
  else if ((ret = StatDescriptor::CreateDescriptor(name, init_val))) {
    ink_mutex_acquire(&m_mux);
    grow_check(ret);
    ink_mutex_release(&m_mux);
  }

  return ret;
}

CST_INLINE CoupledStatsSnapshot::~CoupledStatsSnapshot()
{
  m_src = NULL;
  if (m_stats) {
    delete[]m_stats;
  }
}

CST_INLINE CoupledStatsSnapshot::CoupledStatsSnapshot(CoupledStats * src)
{
  extern ink_mutex
    g_cpl_mux;

  m_fetch = 0;
  m_src = src;
  m_stat_count = src->m_stat_count;

  if (m_stat_count > 0) {
    src->m_snap_taken = true;
    m_stats = NEW(new StatDescriptor[m_stat_count]);

    // make sure we have consistent copy.
    ink_mutex_acquire(&g_cpl_mux);
    for (int idx = 0; idx < src->m_stat_count; idx++) {
      m_stats[idx] = *(src->m_stats[idx]);
    }
    ink_mutex_release(&g_cpl_mux);

    ink_assert(m_stats[0].copy());
  } else {
    m_stats = NULL;
  }
}

#endif // COUPLED_STATS_H
