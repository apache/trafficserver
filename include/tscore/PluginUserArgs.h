/** @file

  Base class and implementation details for the User Args features.

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

#pragma once

#include <array>
#include "tscore/ink_assert.h"

static constexpr int MAX_USER_ARGS_TXN   = 16; /* max number of user arguments for Transactions */
static constexpr int MAX_USER_ARGS_SSN   = 8;  /* max number of user arguments for Sessions (for now) */
static constexpr int MAX_USER_ARGS_VCONN = 4;  /* max number of VConnection user arguments */
// static constexpr int MAX_USER_ARGS_GLB   = 128; /* max number of user arguments, globally */

/**
  This is a mixin class (sort of), implementing the appropriate APIs and data storage for
  a particular user arg table. Used by VConn / Ssn / Txn user arg data.
*/
class PluginUserArgsMixin
{
public:
  virtual void *get_user_arg(unsigned ix) const     = 0;
  virtual void set_user_arg(unsigned ix, void *arg) = 0;
};

template <size_t N> class PluginUserArgs : public virtual PluginUserArgsMixin
{
public:
  void *
  get_user_arg(unsigned ix) const
  {
    ink_assert(ix < user_args.size());
    return this->user_args[ix];
  };

  void
  set_user_arg(unsigned ix, void *arg)
  {
    ink_assert(ix < user_args.size());
    user_args[ix] = arg;
  };

  void
  clear()
  {
    user_args.fill(nullptr);
  }

private:
  std::array<void *, N> user_args{{nullptr}};
};
