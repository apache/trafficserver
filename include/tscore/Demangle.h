/** @file

  Readable C++ type names.

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

#include <string>
#include <typeinfo>

namespace ts
{
/** The readable name of @a type, such as "std::runtime_error".
 *
 * Falls back to @c type.name(), which may be mangled, if demangling fails. This is meant for
 * exception and terminate handlers, so it never throws: if even the fallback cannot be built
 * because memory is exhausted, it returns "<unknown>".
 */
std::string demangle(std::type_info const &type) noexcept;

/** Describe the exception being handled, for a message such as "terminating on an unhandled <this>".
 *
 * The result is "std::runtime_error: what() text" for a std::exception, "exception of type int" for
 * any other thrown type the runtime can name, and "exception" when it cannot. This never throws.
 * Call it from a catch handler or a terminate handler. With no exception in flight it returns
 * "no active exception".
 */
std::string describe_current_exception() noexcept;
} // namespace ts
