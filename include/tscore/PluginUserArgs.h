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
#include "ts/apidefs.h"
#include "tscore/ink_assert.h"
#include "tscore/PluginUserArgs.h"

static constexpr std::array<size_t, TS_USER_ARGS_COUNT> MAX_USER_ARGS = {{
  16, /* max number of user arguments for TXN */
  8,  /* max number of user arguments for SSN */
  4,  /* max number of user arguments for VCONN */
  128 /* max number of user arguments for GLB */
}};

/**
  This is a mixin class (sort of), implementing the appropriate APIs and data storage for
  a particular user arg table. Used by VConn / Ssn / Txn user arg data.
*/
class PluginUserArgsMixin
{
public:
  virtual ~PluginUserArgsMixin()                  = default;
  virtual void *get_user_arg(size_t ix) const     = 0;
  virtual void set_user_arg(size_t ix, void *arg) = 0;
};

template <TSUserArgType I> class PluginUserArgs : public virtual PluginUserArgsMixin
{
public:
  void *
  get_user_arg(size_t ix) const
  {
    ink_release_assert(ix < user_args.size());
    return this->user_args[ix];
  };

  void
  set_user_arg(size_t ix, void *arg)
  {
    ink_release_assert(ix < user_args.size());
    user_args[ix] = arg;
  };

  void
  clear()
  {
    user_args.fill(nullptr);
  }

private:
  std::array<void *, MAX_USER_ARGS[I]> user_args{{nullptr}};
};
