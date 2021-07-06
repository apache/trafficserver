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
 LogFieldAliasMap.cc


 ***************************************************************************/

#include "tscore/ink_config.h"
#include "LogFieldAliasMap.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_memory.h"

void
LogFieldAliasTable::init(size_t numPairs, ...)
{
  ink_assert(numPairs > 0);

  if (m_table) {
    delete[] m_table;
  }

  size_t n;
  va_list ap;
  va_start(ap, numPairs);

  /* A note on the varargs -
     Although IntType is used internally the compiler doesn't know that
     at the call site of this method. Because the numeric values aren't
     explicitly cast, they will come through as "int" sized values.
  */
  m_min = m_max = va_arg(ap, int);
  va_arg(ap, char *); // ignore next arg. for now

  for (n = 1; n < numPairs; n++) {
    IntType val = va_arg(ap, int);
    if (val < m_min) {
      m_min = val;
    } else if (val > m_max) {
      m_max = val;
    }
    va_arg(ap, char *); // ignore next arg. for now
  }

  va_end(ap);
  va_start(ap, numPairs);

  m_entries = m_max - m_min + 1;
  m_table   = new LogFieldAliasTableEntry[m_entries];

  for (n = 0; n < numPairs; n++) {
    IntType val = va_arg(ap, int);
    size_t i    = val - m_min;
    char *name  = va_arg(ap, char *);

    m_table[i].name   = ats_strdup(name);
    m_table[i].length = strlen(name);
    m_table[i].valid  = true;
  }

  va_end(ap);
}
