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
 Stats
 ***************************************************************************/

#ifndef STATS_H
#define STATS_H

// #include <stdio.h>
// #include <stdlib.h>
// #include <limits.h>
// #include <string.h>
// #include <sys/types.h>

#include "ink_resource.h"
#include "ink_assert.h"

#include "I_RecCore.h"
#include "I_RecSignals.h"
#include "I_RecProcess.h"

#ifdef _s_inline
#define ST_INLINE
#else
#define ST_INLINE inline
#endif

class StatDescriptor
{
public:
  enum s_magic
  { MAX_NUM_STATS = 250,
    NULL_VALUE = -1,
    SHALLOW_COPY = -2,
    IN_ERROR = -3,
    ALIVE = -4
  } m_magic;

private:
  static RecRawStatBlock *g_stat_block;
  static StatDescriptor all_stats[MAX_NUM_STATS];
  static volatile int top_stat;

public:
  static StatDescriptor *CreateDescriptor(const char *category, char *name, size_t name_len, int64_t init_value);
  static StatDescriptor *CreateDescriptor(const char *category, char *name, size_t name_len, float init_value);
  static StatDescriptor *CreateDescriptor(const char *name, int64_t init_value);
  static StatDescriptor *CreateDescriptor(const char *name, float init_value);
  static void initialize();
  const char *name() const { return m_name; }

  int64_t int_value() const;
  float flt_value() const;

  bool int_type() const { return m_type == RECD_INT; }
  bool copy() const { return m_magic == SHALLOW_COPY; }
  bool dead() const { return (m_magic == NULL_VALUE || m_magic == IN_ERROR); }
  bool live() const { return (m_magic == ALIVE && m_id >= 0); }

  void increment()
  {
    if (m_type == RECD_INT)
      add((int64_t) 1);
    else
      add((float) 1.0);
  }

  void decrement()
  {
    if (m_type == RECD_INT)
      add((int64_t) - 1);
    else
      add((float) -1.0);
  }

  void subtract(int64_t val)
  {
    add(-val);
  }
  void subtract(float val)
  {
    add(-val);
  }
  void set(int64_t val);
  void set(float val);
  void add(int64_t val);
  void add(float val);
  void commit();

  // to support coupled-stat local copies
 StatDescriptor()
   : m_magic(NULL_VALUE), m_id(0), m_name(NULL), m_type(RECD_NULL)
  { }
  ~StatDescriptor() {
  }                             // [footnote]

  StatDescriptor & operator=(const StatDescriptor & rhs);

private:
  RecData update_value();

  int m_id;
  char *m_name;
  RecDataT m_type;
  RecData m_value;

  // footnote:
  // Statistics cannot be deleted in the current system;
  // this is an artifact of librecords design.  (hence
  // the null destructor).
  //
  // Use of new/delete is prohibitted for this class.
  // We only need to make copies for coupled stat snapshots
  // and during init.
};

ST_INLINE StatDescriptor &
StatDescriptor::operator=(const StatDescriptor & rhs)
{
  if (this != &rhs) {
    switch (rhs.m_magic) {
    case NULL_VALUE:
      m_magic = NULL_VALUE;
      break;

    case IN_ERROR:
      m_magic = IN_ERROR;
      break;

    case ALIVE:
    case SHALLOW_COPY:
      m_magic = rhs.m_magic;
      m_id = rhs.m_id;
      m_type = rhs.m_type;
      m_name = rhs.m_name;
      m_value = rhs.m_value;
      if (m_magic == ALIVE) {
        (void) update_value();
        m_magic = SHALLOW_COPY;
      }
      break;

    default:
      ink_debug_assert(!"bad stat magic");
      m_magic = IN_ERROR;
      break;
    }
  }

  return *this;
}

ST_INLINE int64_t
StatDescriptor::int_value() const
{
  if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    Warning("Attempt to read invalid plugin statistic");
    // ink_debug_assert (0);
    return 0;
  }

  RecData tmp = (m_magic == SHALLOW_COPY) ? m_value : const_cast<StatDescriptor *>(this)->update_value();

  return m_type == RECD_INT ? tmp.rec_int : (int64_t) tmp.rec_float;
}

ST_INLINE float
StatDescriptor::flt_value() const
{
  if (m_magic == NULL_VALUE || m_magic == IN_ERROR) {
    Warning("Attempt to read invalid plugin statistic");
    // ink_debug_assert (0);
    return 0.0;
  }

  RecData tmp = (m_magic == SHALLOW_COPY) ? m_value : const_cast<StatDescriptor *>(this)->update_value();

  return m_type == RECD_INT ? (float) tmp.rec_int : tmp.rec_float;
}

void init_inkapi_stat_system();

#endif // STATS_H
