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

/****************************************************************************

  Error.cc

  This file contains code to manipulate Error


 ****************************************************************************/
#include "proxy/Main.h"
#include "Error.h"
#include <time.h>
#include "ts/ink_platform.h"
#include "ts/ink_assert.h"
#include "ts/ink_thread.h"

ErrorClass::~ErrorClass()
{
}

void
ErrorClass::operator()(const char *aformat_string, ...)
{
  va_list aap;
  va_start(aap, aformat_string);
  format_string = aformat_string;
  raise(aap);
  va_end(aap);
  delete this;
}

void
ErrorClass::raise(va_list ap, const char * /* prefix ATS_UNUSED */)
{
  SrcLoc loc(filename, function_name, line_number);
  diags->print_va(NULL, DL_Fatal, &loc, format_string, ap);
}

// Request Fatal
// Abort the current request, cleanup all related resources.
//
void
RequestFatalClass::raise(va_list ap, const char *prefix)
{
  ErrorClass::raise(ap, prefix ? prefix : "REQUEST FATAL");
  ink_assert(!"RequestFatal");
}

// Thread Fatal
// Abort the current thread, restart within processor
//
void
ThreadFatalClass::raise(va_list ap, const char *prefix)
{
  ErrorClass::raise(ap, prefix ? prefix : "THREAD FATAL");
  ink_assert(!"ThreadFatal");
  ink_thread_exit(0);
}

// Processor Fatal
// Kill and restart the processor
//
void
ProcessorFatalClass::raise(va_list ap, const char *prefix)
{
  ErrorClass::raise(ap, prefix ? prefix : "PROCESSOR FATAL");
  ink_assert(!"ProcessorFatal");
}

// Process Fatal
// Kill and restart the process
//
void
ProcessFatalClass::raise(va_list ap, const char *prefix)
{
  ErrorClass::raise(ap, prefix ? prefix : "PROCESS FATAL");
  // exit(1);
  ink_assert(!"ProcessFatal");
}

// Machine Fatal
// Kill and restart the set of processors on this machine
//
void
MachineFatalClass::raise(va_list ap, const char *prefix)
{
  ErrorClass::raise(ap, prefix ? prefix : "MACHINE FATAL");
  exit(2);
}
