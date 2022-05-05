/** @file

  DbgCtl class header file.

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

#include <ts/ts.h>

// For use with TSDbg().
//
class DbgCtl
{
public:
  // Tag is a debug tag.  Debug output associated with this control will be output when debug output
  // is enabled globally, and the tag matches the configured debug tag regular expression.
  //
  DbgCtl(char const *tag) : _ptr(_get_ptr(tag)) {}

  TSDbgCtl const *
  ptr() const
  {
    return _ptr;
  }

  // Call this when the compiled regex to enable tags may have changed.
  //
  static void update();

private:
  TSDbgCtl const *const _ptr;

  static const TSDbgCtl *_get_ptr(char const *tag);

  class _RegistryAccessor;

  friend TSDbgCtl const *TSDbgCtlCreate(char const *tag);
};
