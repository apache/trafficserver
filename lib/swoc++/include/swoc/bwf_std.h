/** @file

    BufferWriter formatters for types in the std namespace.

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

#include <atomic>
#include "swoc/bwf_base.h"

namespace std
{
template <typename T>
swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, atomic<T> const &v)
{
  return swoc::bwformat(w, spec, v.load());
}
} // end namespace std
