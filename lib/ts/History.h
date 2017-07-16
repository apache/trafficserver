/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef HISTORY_H_84E66E8E_1D2A_438B_A09A_2CD8FB51B209
#define HISTORY_H_84E66E8E_1D2A_438B_A09A_2CD8FB51B209

#include "ts/SourceLocation.h"

#define NO_REENTRANT 99999
#define NO_EVENT NO_REENTRANT
#define HISTORY_DEFAULT_SIZE 65

struct HistoryEntry {
  SourceLocation location;
  unsigned short event = 0;
  short reentrancy     = 0;
};

template <unsigned Count> class History
{
public:
  void
  push_back(const SourceLocation &location, int event, int reentrant = NO_REENTRANT)
  {
    if (overflowed()) {
      return;
    }

    int pos                 = history_pos++ % Count;
    history[pos].location   = location;
    history[pos].event      = (unsigned short)event;
    history[pos].reentrancy = (short)reentrant;
  }

  void
  clear()
  {
    ink_zero(history);
    history_pos = 0;
  }

  bool
  overflowed() const
  {
    return history_pos >= Count;
  }

  unsigned int
  size() const
  {
    return history_pos > Count ? Count : history_pos;
  }

  const HistoryEntry &operator[](unsigned int i) const { return history[i]; }

private:
  HistoryEntry history[Count];

  unsigned int history_pos = 0;
};

#endif /* HISTORY_H_84E66E8E_1D2A_438B_A09A_2CD8FB51B209 */
