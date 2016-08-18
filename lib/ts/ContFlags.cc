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

#include "ContFlags.h"

static ink_thread_key init_thread_key();
static inkcoreapi ink_thread_key flags_data_key = init_thread_key();

static ink_thread_key
init_thread_key()
{
  ink_thread_key_create(&flags_data_key, NULL);
  return flags_data_key;
}

/* Set up a cont_flags entry for this threa */
void
init_cont_flags()
{
  ContFlags new_flags;
  void *val = reinterpret_cast<void *>(static_cast<intptr_t>((new_flags.get_flags())));
  ink_thread_setspecific(flags_data_key, val);
}

void
set_cont_flags(const ContFlags &flags)
{
  void *val = reinterpret_cast<void *>(static_cast<intptr_t>((flags.get_flags())));
  ink_thread_setspecific(flags_data_key, val);
}

void
set_cont_flag(ContFlags::flags flag_bit, bool value)
{
  ContFlags new_flags(reinterpret_cast<intptr_t>(ink_thread_getspecific(flags_data_key)));
  new_flags.set_flag(flag_bit, value);
  void *val = reinterpret_cast<void *>(static_cast<intptr_t>((new_flags.get_flags())));
  ink_thread_setspecific(flags_data_key, val);
}

ContFlags
get_cont_flags()
{
  return ContFlags(reinterpret_cast<intptr_t>(ink_thread_getspecific(flags_data_key)));
}

bool
get_cont_flag(ContFlags::flags flag_bit)
{
  ContFlags flags(reinterpret_cast<intptr_t>(ink_thread_getspecific(flags_data_key)));
  return flags.get_flag(flag_bit);
}
