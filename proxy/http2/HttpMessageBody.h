/** @file

  Routines to construct and manipulate msg bodies and format err responses

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

#ifndef _HttpMessageBody_h_
#define _HttpMessageBody_h_

#include "libts.h"
#include "HTTP.h"
#include "HttpConfig.h"
#include "Error.h"
#include "Main.h"

class HttpMessageBody
{
public:
  static const char *StatusCodeName(HTTPStatus status_code);

  static char *MakeErrorBody(int64_t max_buffer_length,
                             int64_t *resulting_buffer_length,
                             const HttpConfigParams * config,
                             HTTPStatus status_code, char *reason_or_null, char *format, ...)
  {
    va_list va;
    char *ret;

      va_start(va, format);
      ret = MakeErrorBodyVA(max_buffer_length, resulting_buffer_length,
                            config, status_code, reason_or_null, format, va);
      va_end(va);

      return ret;
  }

  static char *MakeErrorBodyVA(int64_t max_buffer_length,
                               int64_t *resulting_buffer_length,
                               const HttpConfigParams * config,
                               HTTPStatus status_code, const char *reason_or_null, const char *format, va_list va);
};

#endif
