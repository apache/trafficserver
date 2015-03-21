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
 *  WebCompatibility.h - cross platform issues dealt with here
 *
 *
 ****************************************************************************/

#ifndef _WEB_COMPATIBILITY_H_
#define _WEB_COMPATIBILITY_H_

#include "ink_platform.h"
#include "ink_defs.h"
#include "ink_inet.h"
#include "ink_string.h"
#include "ink_time.h"

#include "WebGlobals.h"

//-------------------------------------------------------------------------
// types/defines
//-------------------------------------------------------------------------

#define WEB_HANDLE_INVALID -1
typedef int WebHandle;

//-------------------------------------------------------------------------
// WebGetHostname
//-------------------------------------------------------------------------

#if defined(freebsd)
// extern "C" struct hostent *gethostbyaddr_r(const char *addr, int length, int type,
//                                  struct hostent *result, char *buffer, int buflen, int *h_errnop);
#endif

#if defined(solaris)
extern "C" {
struct hostent *gethostbyaddr_r(const char *addr, int length, int type, struct hostent *result, char *buffer, int buflen,
                                int *h_errnop);
}
#endif

char *WebGetHostname_Xmalloc(sockaddr_in *client_info);

//-------------------------------------------------------------------------
// WebFile
//-------------------------------------------------------------------------

WebHandle WebFileOpenR(const char *file);
WebHandle WebFileOpenW(const char *file);
void WebFileClose(WebHandle h_file);
int WebFileRead(WebHandle h_file, char *buf, int size, int *bytes_read);
int WebFileWrite(WebHandle h_file, char *buf, int size, int *bytes_written);
int WebFileImport_Xmalloc(const char *file, char **file_buf, int *file_size);
int WebFileGetSize(WebHandle h_file);
time_t WebFileGetDateGmt(WebHandle h_file);

#endif // _WEB_COMPATIBILITY_H_
