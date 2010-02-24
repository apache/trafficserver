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

#define _c_impl
#include "ink_config.h"
#include "CoupledStats.h"
#include "P_EventSystem.h"

void
CoupledStatsSnapshot::CommitUpdates()
{
  extern ink_mutex g_cpl_mux;

  // make sure we have consistent copy.
  ink_mutex_acquire(&g_cpl_mux);
  for (int idx = 0; idx < m_stat_count; idx++) {
    m_stats[idx].commit();
  }
  ink_mutex_release(&g_cpl_mux);
}

StatDescriptor *
CoupledStatsSnapshot::fetch(const char *name)
{
  if (!name || !*name) {
    return NULL;
  }

  for (int idx = 0; idx < m_stat_count; idx++) {
    if (strcmp(name, m_stats[idx].name()) == 0) {
      return &(m_stats[idx]);
    }
  }

  return NULL;
}

StatDescriptor *
CoupledStatsSnapshot::fetchNext()
{
  if (m_fetch >= m_stat_count) {
    return NULL;
  }

  return &(m_stats[m_fetch++]);
}
