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
#define _s_impl
#include "ink_config.h"
#include "Stats.h"
#include "P_EventSystem.h"

StatDescriptor StatDescriptor::all_stats[StatDescriptor::MAX_NUM_STATS];
RecRawStatBlock *StatDescriptor::g_stat_block;
StatDescriptor G_NULL_STAT;
volatile int StatDescriptor::top_stat = 0;
ink_mutex g_flt_mux;
ink_mutex g_cpl_mux;

// Main.cc call-in point
void
init_inkapi_stat_system()
{
  StatDescriptor::initialize();
  ink_mutex_init(&g_flt_mux, "proxy/stat floating pt. mutex");
  ink_mutex_init(&g_cpl_mux, "proxy/stat coupled stat mutex");
}

void
StatDescriptor::initialize()
{
  // g_stat_block = RecAllocateRawStatBlock(MAX_NUM_STATS);
  // ink_release_assert (g_stat_block);
}

RecData
StatDescriptor::update_value()
{
  RecData retv;

  if (m_magic == NULL_VALUE || m_magic == SHALLOW_COPY || m_magic == IN_ERROR)
    return m_value;;

  ink_release_assert(m_id >= 0 && m_id < MAX_NUM_STATS);

  int rc = REC_ERR_OKAY;

  switch (m_type) {
  case RECD_INT:
    rc = RecGetRecordInt(m_name, &retv.rec_int);
    m_value = retv;
    break;

  case RECD_FLOAT:
    rc = RecGetRecordFloat(m_name, &retv.rec_float);
    m_value = retv;
    break;

  default:
    m_magic = IN_ERROR;
    ink_release_assert(!"Impossible type");
    return m_value;
  }

  if (rc != REC_ERR_OKAY) {
    m_magic = IN_ERROR;
  }

  return retv;
}

StatDescriptor *
StatDescriptor::CreateDescriptor(const char *prefix, char *name, size_t name_len, int64_t init_value)
{
  char *t = name;
  char tv[128];

  ink_debug_assert(prefix && name);
  if (!name || !prefix)
    // return &G_NULL_STAT;
    return NULL;

  if (prefix) {
    t = tv;
    int pln = strlen(prefix);
    int nln = strlen(name);
    if (pln + nln > 126)
      return NULL;              // return &G_NULL_STAT;

    ink_strlcpy(t, prefix, name_len);
    t[pln] = '.'; t[pln + 1] = 0;
    ink_strlcat(t, name, name_len);
  }

  return CreateDescriptor(t, init_value);
}

StatDescriptor *
StatDescriptor::CreateDescriptor(const char *prefix, char *name, size_t name_len, float init_value)
{
  char *t = name;
  char tv[128];

  ink_debug_assert(prefix && name);
  if (!name || !prefix)
    return NULL;
  // return &G_NULL_STAT;

  if (prefix) {
    t = tv;
    int pln = strlen(prefix);
    int nln = strlen(name);
    if (pln + nln > 126)
      return NULL;              // return &G_NULL_STAT;

    ink_strlcpy(t, prefix, name_len);
    t[pln] = '.'; t[pln + 1] = 0;
    ink_strlcat(t, name, name_len);
  }

  return CreateDescriptor(t, init_value);
}

StatDescriptor *
StatDescriptor::CreateDescriptor(const char *name, int64_t init_value)
{
  int n_stat = ink_atomic_increment(&top_stat, 1);
  RecDataT dt;

  if (n_stat >= StatDescriptor::MAX_NUM_STATS) {
    Warning("Plugin stat space exhausted");
    // return &G_NULL_STAT;
    return NULL;
  } else if (RecGetRecordDataType((char *) name, &dt) == REC_ERR_OKAY) {
    Debug("sdk_stats", "Attempt to re-register statistic '%s'", name);
    // return &G_NULL_STAT;
    return NULL;
  } else {
    StatDescriptor &ref = all_stats[n_stat];
    ref.m_id = n_stat;
    ink_assert(ref.m_name == NULL);
    size_t len = strlen(name) + 1;
    ref.m_name = new char[len];
    ink_strlcpy(ref.m_name, name, len);
    ref.m_type = RECD_INT;
    ref.m_value.rec_int = init_value;
    ref.m_magic = ALIVE;

    if (RecRegisterStatInt(RECT_PLUGIN, ref.m_name, init_value, RECP_NON_PERSISTENT) == REC_ERR_FAIL) {
      ref.m_magic = IN_ERROR;
    }
    return &all_stats[n_stat];
  }
}

StatDescriptor*
StatDescriptor::CreateDescriptor(const char *name, float init_value)
{
  int n_stat = ink_atomic_increment(&top_stat, 1);
  RecDataT dt;

  if (n_stat >= StatDescriptor::MAX_NUM_STATS) {
    Warning("Plugin stat space exhausted");
    return NULL;
  } else if (RecGetRecordDataType((char *) name, &dt) == REC_ERR_OKAY) {
    Debug("sdk_stats", "Attempt to re-register statistic '%s'", name);
    // return &G_NULL_STAT;
    return NULL;
  } else {
    StatDescriptor & ref = all_stats[n_stat];
    ref.m_id = n_stat;
    ink_assert(ref.m_name == NULL);
    size_t len = strlen(name) + 1;
    ref.m_name = new char[len];
    ink_strlcpy(ref.m_name, name, len);
    ref.m_type = RECD_FLOAT;
    ref.m_value.rec_float = init_value;
    ref.m_magic = ALIVE;

    if (RecRegisterStatFloat(RECT_PLUGIN, ref.m_name, init_value, RECP_NON_PERSISTENT) == REC_ERR_FAIL) {
      ref.m_magic = IN_ERROR;
    }
    return &all_stats[n_stat];
  }
}

void
StatDescriptor::set(int64_t val)
{
  if (m_magic == SHALLOW_COPY) {
    ink_atomic_swap64(&m_value.rec_int, val);
  } else if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    m_magic = IN_ERROR;
  } else {
    if (RecSetRecordInt(m_name, val) == REC_ERR_FAIL) {
      m_magic = IN_ERROR;
    }
  }
}

void
StatDescriptor::set(float val)
{
  if (m_magic == SHALLOW_COPY) {
    ink_mutex_acquire(&g_flt_mux);
    m_value.rec_float = val;
    ink_mutex_release(&g_flt_mux);
  } else if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    m_magic = IN_ERROR;
  } else {
    if (RecSetRecordFloat(m_name, val) == REC_ERR_FAIL) {
      m_magic = IN_ERROR;
    }
  }
}

void
StatDescriptor::add(int64_t val)
{
  if (m_magic == SHALLOW_COPY) {
    ink_atomic_increment64(&m_value.rec_int, val);
  } else if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    m_magic = IN_ERROR;
  } else {
    RecInt v;
    ink_mutex_acquire(&g_flt_mux);

    int rc = RecGetRecordInt(m_name, &v);
    if (rc == REC_ERR_OKAY) {
      rc = RecSetRecordInt(m_name, v + val);
    }
    ink_mutex_release(&g_flt_mux);
    if (rc == REC_ERR_FAIL) {
      m_magic = IN_ERROR;
    }
  }
}

void
StatDescriptor::add(float val)
{
  if (m_magic == SHALLOW_COPY) {
    ink_mutex_acquire(&g_flt_mux);
    m_value.rec_float += val;
    ink_mutex_release(&g_flt_mux);
  } else if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    m_magic = IN_ERROR;
  } else {
    RecFloat v;
    ink_mutex_acquire(&g_flt_mux);

    int rc = RecGetRecordFloat(m_name, &v);
    if (rc == REC_ERR_OKAY) {
      rc = RecSetRecordFloat(m_name, v + val);
    }
    ink_mutex_release(&g_flt_mux);
    if (rc == REC_ERR_FAIL) {
      m_magic = IN_ERROR;
    }
  }
}

void
StatDescriptor::commit()
{
  if (m_magic == SHALLOW_COPY) {
    if (m_type == RECD_INT) {
      if (RecSetRecordInt(m_name, m_value.rec_int) == REC_ERR_FAIL) {
        m_magic = IN_ERROR;
      }
    } else {
      if (RecSetRecordFloat(m_name, m_value.rec_float) == REC_ERR_FAIL) {
        m_magic = IN_ERROR;
      }
    }
  } else {
    // Warning ("Stat update failed");
    ink_debug_assert(!"broken");
    return;
  }
}
