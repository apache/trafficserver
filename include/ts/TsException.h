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

#pragma once

/** @file
    Apache Traffic Server Exceptions.
 */

#include <stddef.h>
#include <unistd.h>

namespace ts
{
/** Base class for ATS exception.
    Clients should subclass as appropriate. This is intended to carry
    pre-allocated text along so that it can be thrown without any
    addditional memory allocation.
*/
class Exception
{
public:
  /// Default constructor.
  Exception();
  /// Construct with alternate @a text.
  Exception(const char *text ///< Alternate text for exception.
  );

  static const char *const DEFAULT_TEXT;

protected:
  const char *m_text;
};

// ----------------------------------------------------------
// Inline implementations.

inline Exception::Exception() : m_text(DEFAULT_TEXT) {}
inline Exception::Exception(const char *text) : m_text(text) {}
} // namespace ts
