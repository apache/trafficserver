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
 *  WebHttpAuth.h - code to handle administrative access to the web-ui
 *  
 * 
 ****************************************************************************/

#ifndef _WEB_HTTP_AUTH_H_
#define _WEB_HTTP_AUTH_H_

#include "INKMgmtAPI.h"

#include "P_RecCore.h"

#define WEB_HTTP_AUTH_USER_MAX               16
#define WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN     INK_ENCRYPT_PASSWD_LEN

#define WEB_HTTP_AUTH_ACCESS_NONE            0
#define WEB_HTTP_AUTH_ACCESS_MONITOR         1
#define WEB_HTTP_AUTH_ACCESS_CONFIG_VIEW     2
#define WEB_HTTP_AUTH_ACCESS_CONFIG_CHANGE   3
#define WEB_HTTP_AUTH_ACCESS_MODES           4

struct WebHttpAuthUser
{
  char user[WEB_HTTP_AUTH_USER_MAX + 1];
  char encrypt_passwd[WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN + 1];
  int access;
};

struct WebHttpContext;

void WebHttpAuthInit();
int WebHttpAuthenticate(WebHttpContext * whc);
#ifdef OEM
int WebHttpAuthenticateWithoutNewSession(WebHttpContext * whc);
#endif

#endif // _WEB_HTTP_AUTH_H_
