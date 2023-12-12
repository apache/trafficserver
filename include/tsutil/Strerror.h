/** @file

  A convenience wrapper for the thread-safe strerror_r() function, either
  GNU or XSI version.  Allows the avoidance of use of the thread-unsafe
  strerror() function.

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

#include <string.h>

namespace ts
{
// Typically this class is used to create anonymous temporaries, for example:
//
// if ((fd = open(file_path, O_RDONLY)) < 0) {
//   Error("%s Can not open %s file : %s", module_name, file_path, Strerror(errno).c_str());
//   return nullptr;
// }
//
class Strerror
{
public:
  Strerror(int err_num)
  {
    // Make sure there are no unused function warnings.
    //
    static_cast<void>(_handle(0));
    static_cast<void>(_handle(nullptr));

    // Handle either GNU or XSI version of strerror_r().
    //
    if (!_handle(strerror_r(err_num, _buf, sizeof(_buf)))) {
      _c_str = "strerror_r() call failed";
    }
  }

  char const *
  c_str() const
  {
    return _c_str;
  }

private:
  char _buf[256];
  char const *_c_str;

  // For XSI-compliant strerror_r().
  //
  bool
  _handle(int retval)
  {
    if (0 != retval) {
      return false;
    }
    _buf[sizeof(_buf) - 1] = '\0';
    _c_str                 = _buf;
    return true;
  }

  // For GNU-specific strerror_r().
  //
  bool
  _handle(char *retval)
  {
    _buf[sizeof(_buf) - 1] = '\0';
    _c_str                 = retval;
    return retval != nullptr;
  }
};

} // end namespace ts
