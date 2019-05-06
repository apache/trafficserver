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

 ContFlags.h

 Thread local storage for a set of flags that are updated based the continuation
 currently running in the thread.  These flags are handy if the data is needed
 as a "global" in parts of the code where the original net VC is not available.
 Storing the flags in the void * of the thread key directly. I assume we are on
 at least a 32 bit architecture, so the rawflags size is 32 bits.
 ****************************************************************************/

#pragma once

#include "ink_thread.h"

class ContFlags
{
public:
  enum flags { DEBUG_OVERRIDE = 0, DISABLE_PLUGINS = 1, LAST_FLAG };

  ContFlags() {}
  ContFlags(ContFlags const &that) = default;
  ContFlags(uint32_t in_flags) : raw_flags(in_flags) {}
  void
  set_flags(uint32_t new_flags)
  {
    raw_flags = new_flags;
  }
  ContFlags &
  operator=(ContFlags const &other)
  {
    this->set_flags(other.get_flags());
    return *this;
  }

  uint32_t
  get_flags() const
  {
    return raw_flags;
  }
  void
  set_flag(enum flags flag_bit, bool value)
  {
    if (flag_bit >= 0 && flag_bit < LAST_FLAG) {
      if (value)
        raw_flags |= (1 << flag_bit);
      else
        raw_flags &= ~(1 << flag_bit);
    }
  }
  bool
  get_flag(enum flags flag_bit) const
  {
    if (flag_bit >= 0 && flag_bit < LAST_FLAG) {
      return (raw_flags & (1 << flag_bit)) != 0;
    } else {
      return false;
    }
  }
  bool
  is_set() const
  {
    return raw_flags != 0;
  }

private:
  uint32_t raw_flags = 0;
};

void set_cont_flags(const ContFlags &flags);
void set_cont_flag(ContFlags::flags flag_bit, bool value);
ContFlags get_cont_flags();
bool get_cont_flag(ContFlags::flags flag_bit);
