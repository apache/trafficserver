/** @file

  Header file for HttpBodyFactory module.

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

  HttpBodyFactory.h

  This implements a user-customizable response message generation system.

  The concept is simple.  Error/response messages are classified into
  several types, each given a name, such as "request/header_error".

  The HttpBodyFactory can build a message body for each response type.
  The user can create custom message body text for each type (stored
  in a text file directory), containing templates with space-holders for
  variables which are inline-substituted with curent values.  The resulting
  body is dynamically allocated and returned.

 ****************************************************************************/

#ifndef _HttpBodyFactory_h_
#define _HttpBodyFactory_h_

#include "HttpTransact.h"

////////////////////////////////////////////////////////////////////////
//
//      The HttpBodyFactory module keeps track of all the response body
//      templates, and provides the methods to create response bodies.
//
//      Once the HttpBodyFactory module is initialized, and the template
//      data has been loaded, the module allows the caller to make error
//      message bodies w/fabricate_with_old_api
//
////////////////////////////////////////////////////////////////////////

namespace HttpBodyFactory
{
void init();

char *fabricate_with_old_api(const char *type, HttpTransact::State *context, int64_t max_buffer_length,
                             int64_t *resulting_buffer_length, char *content_language_out_buf, size_t content_language_buf_size,
                             char *content_type_out_buf, size_t content_type_buf_size, int format_size, const char *format);
}

#endif
