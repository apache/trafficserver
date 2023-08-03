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
  DbgCtl(char const *tag) : _ptr{_new_reference(tag)} {}

  ~DbgCtl() { _rm_reference(); }

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

  static const TSDbgCtl *_new_reference(char const *tag);

  static void _rm_reference();

  class _RegistryAccessor;

  friend TSDbgCtl const *TSDbgCtlCreate(char const *tag);

  friend void TSDbgCtlDestroy(TSDbgCtl const *dbg_ctl);

public:
  // When loading an ATS plugin with dlopen(), an instance of this class should exist in the stack.  This will prevent
  // https://github.com/apache/trafficserver/issues/10129 .  It will prevent DbgCtl member functions from indiredtly
  // calling Regex::compile(), which calls a function that defines a thread_local variable.  Such functions try to lock
  // the same global mutex (in the C/C++ runtime) that is locked when dlopen() is in progress.  This prevents a deadlock
  // where shared library static initialization is holding the global mutex and waiting on the Registry mutex, and
  // a DbgCtl intanstantiantion has called Regex::compile() in a different thread, is holding the Registry mutex, and
  // waiting on the global mutex.
  //
  class Guard_dlopen
  {
  public:
    Guard_dlopen();
    ~Guard_dlopen();

    // No copying.
    Guard_dlopen(Guard_dlopen const &)            = delete;
    Guard_dlopen &operator=(Guard_dlopen const &) = delete;

  private:
    _RegistryAccessor *rap;
  };
};
