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

/*****************************************************************************

  ink_sprintf.h

  This file implements some Inktomi variants of sprintf, to do bounds
  checking and length counting.


  ****************************************************************************/

#pragma once

#include <cstdio>
#include <cstdarg>
#include "ts/ink_apidefs.h"
#include "ts/ink_defs.h"

int ink_bsprintf(char *buffer, const char *format, ...) TS_PRINTFLIKE(2, 3);
int ink_bvsprintf(char *buffer, const char *format, va_list ap);
