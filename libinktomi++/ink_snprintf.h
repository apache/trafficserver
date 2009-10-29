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

//-*-c++-*-
#ifndef _ink_snprintf_h_
#define _ink_snprintf_h_


#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ink_apidefs.h"
#include "ink_defs.h"

int
ink_snprintf(char *str, size_t count, const char *fmt, ...)
PRINTFLIKE(3, 4);
int
ink_vsnprintf(char *str, size_t count, const char *fmt, va_list arg);

//
// These calls are like tha above, but take no buffer size..
//
int
ink_sprintf(char *str, const char *fmt, ...)
PRINTFLIKE(2, 3);
int
ink_vsprintf(char *str, const char *fmt, va_list arg);

//
// And these do file I/O
//
int
ink_fprintf(FILE *, const char *fmt, ...)
PRINTFLIKE(2, 3);
int
ink_vfprintf(FILE *, const char *fmt, va_list arg);
int
ink_printf(const char *fmt, ...)
PRINTFLIKE(1, 2);

#endif
