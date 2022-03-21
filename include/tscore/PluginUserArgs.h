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

/** Stagger each user argument value so we can detect mismatched
 * indices.
 *
 * For example, say a plugin associates data with both sessions and
 * transactions and that its session index is 2 and its transaction index is 4.
 * In this case, we'll hand back to the plugin 2002 for its session index and
 * 1004 for its transaction index. If it then accidentally uses its session
 * index to reference its transaction index, it will pass back 2002 instead of
 * 1004, which we will identify as belonging to the wrong user argument type
 * because it is in the 2000 session block rather than the expected 1000
 * transaction block.
 *
 * Note that these higher value 1000 block indices are only used when
 * interfacing with the plugin. Internally the lower valued index is used.
 * That is, for a transaction, expect internally a value of 3 instead of 1003.
 */
static constexpr size_t
get_user_arg_offset(TSUserArgType type)
{
  // TS_USER_ARGS_TXN indices begin at 1000, TS_USER_ARGS_SSN begin at 2000,
  // etc.
  return (static_cast<size_t>(type) + 1) * 1000;
}

/** Verify that the user passed in an index whose value corresponds with the
 * type. See the comment above the declaration of get_user_arg_offset for the
 * intention behind this.
 */
static constexpr inline bool
SanityCheckUserIndex(TSUserArgType type, int idx)
{
  int const block_start = get_user_arg_offset(type);
  return idx >= block_start && idx < block_start + 1000;
}

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
  get_user_arg(size_t ix) const override
  {
    ink_release_assert(SanityCheckUserIndex(I, ix));
    ix -= get_user_arg_offset(I);
    ink_release_assert(ix < user_args.size());
    return this->user_args[ix];
  };

  void
  set_user_arg(size_t ix, void *arg) override
  {
    ink_release_assert(SanityCheckUserIndex(I, ix));
    ix -= get_user_arg_offset(I);
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
