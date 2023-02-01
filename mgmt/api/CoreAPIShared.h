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
 * Filename: CoreAPIShared.h
 * Purpose: This file contains functions that are same for local and remote API
 * Created: 01/29/00
 * Created by: Lan Tran
 *
 *
 ***************************************************************************/

#pragma once

#include "mgmtapi.h"

// used by TSReadFromUrl
#define HTTP_DIVIDER "\r\n\r\n"
#define URL_BUFSIZE  65536 // the max. length of URL obtainable (in bytes)
#define URL_TIMEOUT  5000  // the timeout value for send/recv HTTP in ms
#define HTTP_PORT    80
#define BUFSIZE      1024

// used by TSReadFromUrl
TSMgmtError parseHTTPResponse(char *buffer, char **header, int *hdr_size, char **body, int *bdy_size);
TSMgmtError readHTTPResponse(int sock, char *buffer, int bufsize, uint64_t timeout);
TSMgmtError sendHTTPRequest(int sock, char *request, uint64_t timeout);
int connectDirect(const char *host, int port, uint64_t timeout);
