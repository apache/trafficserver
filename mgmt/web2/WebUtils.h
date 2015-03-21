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

#ifndef _WEB_UTILS_H_
#define _WEB_UTILS_H_

/****************************************************************************
 *
 *  WebUtils.h - Misc Utility Functions for the web server internface
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "MgmtDefs.h"

struct WebContext;

/* Ugly Hack - declare
 *  SSLcon as void* instead of SSL since this prevents  us from
 *  including ssl.h right here which creates a whole bunch of
 *  nasty problem to MD5 conflicts with ink_code.h.
 */
struct SocketInfo {
  int fd;
  void *SSLcon; /* Currently unused */
};

ssize_t socket_write(SocketInfo socketD, const char *buf, size_t nbyte);
ssize_t socket_read(SocketInfo socketD, char *buf, size_t nbyte);
int sigfdrdln(SocketInfo socketD, char *s, int len);

#endif
