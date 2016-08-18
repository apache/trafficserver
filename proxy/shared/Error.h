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

  Error.h

  The objective of the error system is to enable graceful recovery
  from all errors.


 ****************************************************************************/

#if !defined _Error_h_
#define _Error_h_

#include "ts/ink_platform.h"
#include "ts/Diags.h"

//////////////////////////////////////////////////////////////////////////////
//
// Base Error class (abstract)
//
//////////////////////////////////////////////////////////////////////////////

struct ErrorClass {
  const char *filename;
  int line_number;
  const char *function_name;

  const char *format_string;

  ErrorClass(const char *afile, int aline, const char *afunction)
    : filename(afile), line_number(aline), function_name(afunction), format_string(NULL)
  {
  }

  void operator()(const char *aformat_string...);

  virtual ~ErrorClass();
  virtual void raise(va_list ap, const char *prefix = NULL);
};

#if !defined(__GNUC__)
#define __FUNCTION__ NULL
#endif

//////////////////////////////////////////////////////////////////////////////
//
// Fatal Messages (abstract)
//
//////////////////////////////////////////////////////////////////////////////

struct FatalClass : public ErrorClass {
  FatalClass(const char *afile, int aline, const char *afunction) : ErrorClass(afile, aline, afunction) {}
};

//////////////////////////////////////////////////////////////////////////////
//
// RequestFatal Messages
//
//////////////////////////////////////////////////////////////////////////////

struct RequestFatalClass : public FatalClass {
  virtual void raise(va_list ap, const char *prefix = NULL);
  RequestFatalClass(const char *afile, int aline, const char *afunction) : FatalClass(afile, aline, afunction) {}
};

#define RequestFatal (*(new RequestFatalClass(__FILE__, __LINE__, __FUNCTION__)))

//////////////////////////////////////////////////////////////////////////////
//
// ThreadFatal Messages
//
//////////////////////////////////////////////////////////////////////////////

struct ThreadFatalClass : public FatalClass {
  virtual void raise(va_list ap, const char *prefix = NULL);
  ThreadFatalClass(const char *afile, int aline, const char *afunction) : FatalClass(afile, aline, afunction) {}
};

#define ThreadFatal (*(new ThreadFatalClass(__FILE__, __LINE__, __FUNCTION__)))

//////////////////////////////////////////////////////////////////////////////
//
// ProcessorFatal Messages
//
//////////////////////////////////////////////////////////////////////////////

struct ProcessorFatalClass : public FatalClass {
  virtual void raise(va_list ap, const char *prefix = NULL);
  ProcessorFatalClass(const char *afile, int aline, const char *afunction) : FatalClass(afile, aline, afunction) {}
};

#define ProcessorFatal (*(new ProcessorFatalClass(__FILE__, __LINE__, __FUNCTION__)))

//////////////////////////////////////////////////////////////////////////////
//
// ProcessFatal Messages
//
//////////////////////////////////////////////////////////////////////////////

struct ProcessFatalClass : public FatalClass {
  virtual void raise(va_list ap, const char *prefix = NULL);
  ProcessFatalClass(const char *afile, int aline, const char *afunction) : FatalClass(afile, aline, afunction) {}
};

#define ProcessFatal (*(new ProcessFatalClass(__FILE__, __LINE__, __FUNCTION__)))

//////////////////////////////////////////////////////////////////////////////
//
// MachineFatal Messages
//
//////////////////////////////////////////////////////////////////////////////

struct MachineFatalClass : public FatalClass {
  virtual void raise(va_list ap, const char *prefix = NULL);
  MachineFatalClass(const char *afile, int aline, const char *afunction) : FatalClass(afile, aline, afunction) {}
};

#define MachineFatal (*(new MachineFatalClass(__FILE__, __LINE__, __FUNCTION__)))

#endif /*_Error_h_*/
