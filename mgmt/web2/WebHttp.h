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
 *
 *  WebHttp.h - code to process requests, and create responses
 *
 *
 ****************************************************************************/

#ifndef _WEB_HTTP_H_
#define _WEB_HTTP_H_

#include "WebGlobals.h"
#include "WebHttpContext.h"
#include "WebHttpMessage.h"

#include "P_RecCore.h"

void WebHttpInit();
void WebHttpHandleConnection(WebHttpConInfo *whci);

void WebHttpSetErrorResponse(WebHttpContext *whc, HttpStatus_t error);
char *WebHttpAddDocRoot_Xmalloc(WebHttpContext *whc, const char *file, int file_len);
#endif // _WEB_HTTP_H_
