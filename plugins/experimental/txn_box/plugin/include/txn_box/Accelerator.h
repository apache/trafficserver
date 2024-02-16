/**
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

  Copyright 2020 Verizon Media
*/
#pragma once

#include <memory>
#include <functional>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"

class Comparison;

/// Base class for Accelerator implementations.
class Accelerator
{
  using self_type = Accelerator;

public:
  /// Number of defined Accelerators.
  static constexpr size_t N_ACCELERATORS = 1;

  /// Index for @c StringAccelerator.
  static constexpr size_t BY_STRING = 0;

  /// Array for counting the number of candidate comparisons.
  using Counters = std::array<unsigned, Accelerator::N_ACCELERATORS>;

  /// Handle to an Accelerator instance.
  using Handle = std::unique_ptr<Accelerator>;

  /// Construct a specific type of Accelerator.
  using Builder = std::function<swoc::Rv<Handle>()>;

protected:
  static std::array<Builder, N_ACCELERATORS> _factory;
};

// --- //

class StringAccelerator : public Accelerator
{
  using self_type  = StringAccelerator;
  using super_type = Accelerator;

  using Errata   = swoc::Errata;
  using TextView = swoc::TextView;

public:
  StringAccelerator() = default;

  void match_exact(TextView text, Comparison *cmp);
  void match_prefix(TextView text, Comparison *cmp);
  void match_suffix(TextView text, Comparison *cmp);

  /** Find @a text in @a this.
   *
   * @param text Text to match.
   * @return The best match @c Comparison for @a text.
   */
  Comparison *operator()(TextView text) const;
};
