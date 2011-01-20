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
 *  WebHttpAuth.cc - code to handle administrative access to the web-ui
 *
 *
 ****************************************************************************/

#include "libts.h"
#include "ink_platform.h"
#include "ink_base64.h"
#include "ink_code.h"
#include "TextBuffer.h"
#include "Diags.h"

#include "mgmtapi.h"
#include "LocalManager.h"

#include "WebGlobals.h"
#include "WebHttp.h"
#include "WebHttpAuth.h"
#include "WebHttpContext.h"
#include "WebHttpMessage.h"
#include "WebMgmtUtils.h"


//-------------------------------------------------------------------------
// globals
//-------------------------------------------------------------------------

static InkHashTable *g_auth_bypass_ht = 0;

//-------------------------------------------------------------------------
// WebHttpAuthInit
//-------------------------------------------------------------------------

void
WebHttpAuthInit()
{

  // clients will be allowed access to the following items regardess
  // of authentication privileges and settings
  g_auth_bypass_ht = ink_hash_table_create(InkHashTableKeyType_String);
  // splash page items
  ink_hash_table_insert(g_auth_bypass_ht, "/", NULL);
  ink_hash_table_insert(g_auth_bypass_ht, "/index.ink", NULL);
  ink_hash_table_insert(g_auth_bypass_ht, "/images/ink_logo_slim.gif", NULL);
  ink_hash_table_insert(g_auth_bypass_ht, "/images/ink_top_internet.jpg", NULL);
  // snapshotting of java charts
  ink_hash_table_insert(g_auth_bypass_ht, "/charting/chartsnap.cgi", NULL);

  return;

}

//-------------------------------------------------------------------------
// WebHttpAuthenticate
//-------------------------------------------------------------------------

int
WebHttpAuthenticate(WebHttpContext * whc)
{

  char *file;
  char *user = NULL;
  char *passwd = NULL;
  char *encrypt_passwd = NULL;
  char *uuencode_buf;
  char uudecode_buf[MAX_VAL_LENGTH];
  char *uudecode_buf_p = uudecode_buf;
  char **strtok_arg = &uudecode_buf_p;
  void *dummy;

  bool found;
  char *product_name;

  char empty_string[2];
  *empty_string = '\0';

  MgmtHashTable *ht = whc->other_users_ht;
  InkHashTableEntry *hte;
  InkHashTableIteratorState htis;
  WebHttpAuthUser *au;


  // check backdoor items first
  file = (char *) (whc->request->getFile());
  if (ink_hash_table_lookup(g_auth_bypass_ht, file, (void **) &dummy)) {
    goto Lauthenticate;
  }
  // get authentication header
  uuencode_buf = (char *) (whc->request->getAuthMessage());
  if (uuencode_buf == NULL) {
    goto Lchallenge;
  }
  // decode user and password
  ink_base64_decode(uuencode_buf, MAX_VAL_LENGTH, (unsigned char *) uudecode_buf);
  user = ink_strtok_r(uudecode_buf, ":", strtok_arg);
  passwd = ink_strtok_r(NULL, "", strtok_arg);
  Debug("web_auth", "[WebHttpAuthenticate] user (%s), passwd (%s)",
        user ? user : "user is NULL", passwd ? passwd : "passwd is NULL");

  // handle null users
  if (user == NULL) {
    strncpy(whc->current_user.user, "NULL", WEB_HTTP_AUTH_USER_MAX);
    whc->current_user.access = WEB_HTTP_AUTH_ACCESS_NONE;
    goto Laccess_defined;
  }
  // store the current-user
  strncpy(whc->current_user.user, user, WEB_HTTP_AUTH_USER_MAX);

  // handle null passwords
  if (passwd == NULL) {
    // special case for admin user (so can reset the admin password to nothing)
    if ((strncmp(user, whc->admin_user.user, WEB_HTTP_AUTH_USER_MAX) == 0) &&
        ((whc->admin_user.encrypt_passwd)[0] == '\0')) {
      whc->current_user.access = WEB_HTTP_AUTH_ACCESS_CONFIG_CHANGE;
      Debug("web_auth", "[WebHttpAuthenticate] "
            "adminContext.admin.passwd is NULL; allowing access (%d) to '%s'",
            whc->current_user.access, whc->admin_user.user);
      goto Laccess_defined;
    }
    // otherwise, null passwords are treated normally for everyone else
    passwd = empty_string;
  }

  INKEncryptPassword(passwd, &encrypt_passwd);

  Debug("web_auth", "[WebHttpAuthenticate] encrypt_passwd (%s), "
        "admin.encrypt_passwd (%s)\n", encrypt_passwd, whc->admin_user.encrypt_passwd);

  // check against admin user/password
  if ((strncmp(user, whc->admin_user.user, WEB_HTTP_AUTH_USER_MAX) == 0) &&
      (strncmp(encrypt_passwd, whc->admin_user.encrypt_passwd, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) == 0)) {
    whc->current_user.access = WEB_HTTP_AUTH_ACCESS_CONFIG_CHANGE;
    goto Laccess_defined;
  }
  // check against additional users
  for (hte = ht->mgmt_hash_table_iterator_first(&htis); hte != NULL; hte = ht->mgmt_hash_table_iterator_next(&htis)) {
    au = (WebHttpAuthUser *) ht->mgmt_hash_table_entry_value(hte);
    if (strcmp(user, au->user) == 0) {
      if (strncmp(encrypt_passwd, au->encrypt_passwd, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) == 0) {
        whc->current_user.access = au->access;
        goto Laccess_defined;
      }
    }
  }

  // Fix INKqa02963 - unescapify the username and passwd since it
  // could have been entered as part of a URL
  substituteUnsafeChars(user);
  substituteUnsafeChars(passwd);

  xfree(encrypt_passwd);
  INKEncryptPassword(passwd, &encrypt_passwd);

  // FIXME: Yucky cut-and-paste code below!!!

  // check against admin user/password
  if ((strncmp(user, whc->admin_user.user, WEB_HTTP_AUTH_USER_MAX) == 0) &&
      (strncmp(encrypt_passwd, whc->admin_user.encrypt_passwd, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) == 0)) {
    whc->current_user.access = WEB_HTTP_AUTH_ACCESS_CONFIG_CHANGE;
    goto Laccess_defined;
  }
  // check against additional users
  for (hte = ht->mgmt_hash_table_iterator_first(&htis); hte != NULL; hte = ht->mgmt_hash_table_iterator_next(&htis)) {
    au = (WebHttpAuthUser *) ht->mgmt_hash_table_entry_value(hte);
    if (strcmp(user, au->user) == 0) {
      if (strncmp(encrypt_passwd, au->encrypt_passwd, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) == 0) {
        whc->current_user.access = au->access;
        goto Laccess_defined;
      }
    }
  }

  // didn't find anyone
  whc->current_user.access = WEB_HTTP_AUTH_ACCESS_NONE;

Laccess_defined:
  if (encrypt_passwd != NULL)
    xfree(encrypt_passwd);
  Debug("web_auth", "[WebHttpAuthenticate] access defined to be: %d\n", whc->current_user.access);

  if (whc->current_user.access == WEB_HTTP_AUTH_ACCESS_NONE)
    goto Lchallenge;
  if ((strncmp(file, "/configure/", strlen("/configure/")) == 0) &&
      (whc->current_user.access == WEB_HTTP_AUTH_ACCESS_MONITOR)) {
    goto Lchallenge;
  }
  //kwt
  if ((whc->request->getMethod() == METHOD_POST) && (whc->current_user.access != WEB_HTTP_AUTH_ACCESS_CONFIG_CHANGE)
      && (strncmp(file, "/charting/chart.cgi", strlen("/charting/chart.cgi")) != 0)) {
    goto Lchallenge;
  }
  //kwt

  goto Lauthenticate;

Lauthenticate:

  return WEB_HTTP_ERR_OKAY;

Lchallenge:

  found = false;
  found = (RecGetRecordString_Xmalloc("proxy.config.product_name", &product_name)
           == REC_ERR_OKAY);
  whc->response_hdr->setStatus(STATUS_UNAUTHORIZED);
  if (found && product_name) {
    whc->response_hdr->setRealm(product_name);
    xfree(product_name);
  } else {
    whc->response_hdr->setRealm("Traffic_Server");
  }
  WebHttpSetErrorResponse(whc, STATUS_UNAUTHORIZED);
  return WEB_HTTP_ERR_FAIL;

}



