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

/***************************************/

#include "ink_config.h"
#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "WebReconfig.h"
#include "LocalManager.h"
#include "MgmtUtils.h"
#include "WebGlobals.h"
#include "WebIntrMain.h"
#include "WebMgmtUtils.h"
#include "MgmtAllow.h"
#include "Diags.h"
#include "SimpleTokenizer.h"
#include "WebCompatibility.h"
#include "MgmtSocket.h"
#include "WebHttpRender.h"

/****************************************************************************
 *
 *  WebReconfig.cc - code to handle config vars that can change on the fly
 *
 *
 ****************************************************************************/

// Since we don't want to steal the manager's main thread we get
// config callbacks, set up an array to store the callback info and
// then read it periodically
#define ADV_UI_ENABLED 0
#define AUTH_ENABLED_CB 1
#define AUTH_ADMIN_USER_CB 2
#define AUTH_ADMIN_PASSWD_CB 3
#define AUTH_OTHER_USERS_CB 4
#define LANG_DICT_CB 5
#define LOAD_FACTOR_CB 6
#define MGMT_IP_ALLOW 7
#define REFRESH_RATE_CB 8
#define SSL_ENABLED_CB 9
#define UPDATE_ARRAY_SIZE 10

int webConfigChanged = 0;
static int updateArray[UPDATE_ARRAY_SIZE];

static int
WebConfigCB(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(data);
  long index = (long) cookie;
  updateArray[index] = 1;
  webConfigChanged = 1;

  return 1;
}

void
setUpWebCB()
{
  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.basic_auth", WebConfigCB, (void *) AUTH_ENABLED_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.admin_user", WebConfigCB, (void *) AUTH_ADMIN_USER_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.admin_password", WebConfigCB, (void *) AUTH_ADMIN_PASSWD_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.use_ssl", WebConfigCB, (void *) SSL_ENABLED_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.ui_refresh_rate", WebConfigCB, (void *) REFRESH_RATE_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.load_factor", WebConfigCB, (void *) LOAD_FACTOR_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.lang_dict", WebConfigCB, (void *) LANG_DICT_CB)
             == REC_ERR_OKAY);

  ink_assert(RecRegisterConfigUpdateCb("proxy.config.admin.advanced_ui", WebConfigCB, (void *) ADV_UI_ENABLED)
             == REC_ERR_OKAY);

}

void
markMgmtIpAllowChange()
{
  updateArray[MGMT_IP_ALLOW] = 1;
  webConfigChanged = 1;
}

void
markAuthOtherUsersChange()
{
  updateArray[AUTH_OTHER_USERS_CB] = 1;
  webConfigChanged = 1;
}

// void updateWebConfig()
//
//  Called when one of web variables that is configurable
//    on the fly has changed
//
// CALLEE MUST BE HOLDING wGlobals.serviceThrLock;
//
void
updateWebConfig()
{

  webConfigChanged = 0;
  if (updateArray[AUTH_ENABLED_CB] != 0) {
    updateArray[AUTH_ENABLED_CB] = 0;
    configAuthEnabled();
  }

  if (updateArray[AUTH_ADMIN_USER_CB] != 0) {
    updateArray[AUTH_ADMIN_USER_CB] = 0;
    configAuthAdminUser();
  }

  if (updateArray[AUTH_ADMIN_PASSWD_CB] != 0) {
    updateArray[AUTH_ADMIN_PASSWD_CB] = 0;
    configAuthAdminPasswd();
  }

  if (updateArray[AUTH_OTHER_USERS_CB] != 0) {
    updateArray[AUTH_OTHER_USERS_CB] = 0;
    configAuthOtherUsers();
  }

  if (updateArray[LANG_DICT_CB] != 0) {
    updateArray[LANG_DICT_CB] = 0;
    configLangDict();
  }

  if (updateArray[LOAD_FACTOR_CB] != 0) {
    updateArray[LOAD_FACTOR_CB] = 0;
    configLoadFactor();
  }

  if (updateArray[MGMT_IP_ALLOW] != 0) {
    updateArray[MGMT_IP_ALLOW] = 0;
    configMgmtIpAllow();
  }

  if (updateArray[REFRESH_RATE_CB] != 0) {
    updateArray[REFRESH_RATE_CB] = 0;
    configRefreshRate();
  }

  if (updateArray[SSL_ENABLED_CB] != 0) {
    updateArray[SSL_ENABLED_CB] = 0;
    configSSLenable();
  }


  if (updateArray[ADV_UI_ENABLED] != 0) {
    updateArray[ADV_UI_ENABLED] = 0;
    configUI();
  }

}

// The following functions all manipulate the adminContext
// Also called in webIntr_main()
void
configUI()
{
  RecInt AdvUIEnabled;
  ink_assert(RecGetRecordInt("proxy.config.admin.advanced_ui", &AdvUIEnabled)
             == REC_ERR_OKAY);

  adminContext.AdvUIEnabled = (int) AdvUIEnabled;
  Debug("ui", "configUI: advancied ui(%d)\n", adminContext.AdvUIEnabled);

  RecInt FeatureSet;
  ink_assert(RecGetRecordInt("proxy.config.feature_set", &FeatureSet)
             == REC_ERR_OKAY);
  adminContext.FeatureSet = (int) FeatureSet;
  Debug("ui", "configUI: feature_set(%d)\n", adminContext.FeatureSet);
}

void
configAuthEnabled()
{
  RecInt authEnabled;
  ink_assert(RecGetRecordInt("proxy.config.admin.basic_auth", &authEnabled)
             == REC_ERR_OKAY);

  adminContext.adminAuthEnabled = (int) authEnabled;
}

void
configAuthAdminUser()
{
  RecString user = NULL;
  ink_assert(RecGetRecordString_Xmalloc("proxy.config.admin.admin_user", &user)
             == REC_ERR_OKAY);

  if (user == NULL) {
    adminContext.admin_user.user[0] = '\0';
  } else {
    if (strlen(user) > WEB_HTTP_AUTH_USER_MAX) {
      user[WEB_HTTP_AUTH_USER_MAX] = '\0';
      mgmt_log(stderr, "admin_user name length too long, truncating to '%s'\n", user);
    }
    ink_strncpy(adminContext.admin_user.user, user, WEB_HTTP_AUTH_USER_MAX);
  }
  if (user) {
    xfree(user);
  }
}

void
configAuthAdminPasswd()
{
  RecString passwd = NULL;
  ink_assert(RecGetRecordString_Xmalloc("proxy.config.admin.admin_password", &passwd)
             == REC_ERR_OKAY);

  if (passwd == NULL) {
    adminContext.admin_user.encrypt_passwd[0] = '\0';
  } else {
    if (strlen(passwd) != WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) {
      mgmt_elog(stderr, "Malformed encrypted admin password; length incorrect, '%s'\n", passwd);
      mgmt_elog(stderr, "admin_user access may fail\n");
    }
    ink_strncpy(adminContext.admin_user.encrypt_passwd, passwd, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN);
  }
  if (passwd) {
    xfree(passwd);
  }
}

void
configAuthOtherUsers()
{
  char fpath[FILE_NAME_MAX];
  char *fbuf;
  int64_t fsize;
  MgmtHashTable *ht;
  InkHashTableEntry *hte;
  InkHashTableIteratorState htis;
  char *b, *n;
  SimpleTokenizer st(':');
  int done;
  char *name;
  WebHttpAuthUser *au;
  char *p;
  int fd;
  struct stat stat_buf;
  char error_msg[80];
  bool error;
  MgmtHashTable *blacklist_ht;

  // open other authenticated users config file
  RecString file = NULL;
  int rec_err = RecGetRecordString_Xmalloc("proxy.config.admin.access_control_file", &file);
  if (rec_err != REC_ERR_OKAY)
    return;
  snprintf(fpath, sizeof(fpath), "%s%s%s", mgmt_path, DIR_SEP, file);

#if !defined (_WIN32)
  if ((fd =::mgmt_open(fpath, O_RDONLY)) < 0) {
#else
  if ((fd =::mgmt_open(fpath, O_RDONLY | O_BINARY)) < 0) {
#endif
    mgmt_elog(stderr, "[configAuthOtherUsers] Could not open '%s'\n", fpath);
    return;
  }
  ::fstat(fd, &stat_buf);
  fsize = stat_buf.st_size;
  fbuf = (char *) xmalloc(fsize + 1);
  if (::read(fd, fbuf, fsize) != fsize) {
    xfree(fbuf);
    mgmt_elog(stderr, "[configAuthOtherUsers] Read failed '%s'\n", fpath);
    return;
  }
  fbuf[fsize] = '\0';
  ::close(fd);

  // FIXME: by empting the current hash-table element at a time, and
  // then re-populating it later, we create a small window where
  // access may be denied.  However, this avoids badness that can
  // occur if we atomic swap and delete the old table while someone
  // else is still using the old table.
  ht = adminContext.other_users_ht;
  for (hte = ht->mgmt_hash_table_iterator_first(&htis); hte != NULL; hte = ht->mgmt_hash_table_iterator_next(&htis)) {
    name = (char *) ht->mgmt_hash_table_entry_key(hte);
    au = (WebHttpAuthUser *) ht->mgmt_hash_table_entry_value(hte);
    ht->mgmt_hash_table_delete(name);
    xfree(au);
  }

  // initialize a new blacklist_ht
  blacklist_ht = new MgmtHashTable("blacklist_ht", false, InkHashTableKeyType_String);

  // contruct a new hash-table from our file
  done = false;
  error = false;
  n = fbuf;
  while (!done) {
    b = n;
    // find end of this line and mark it
    while (*n != '\r' && *n != '\n') {
      if (*n == '\0') {
        done = true;
        break;
      }
      n++;
    }
    while (*n == '\r' || *n == '\n') {
      *n = '\0';
      n++;
    }

    // do we have a comment or blank
    if (*b == '#')
      continue;

    // parse
    au = (WebHttpAuthUser *) xmalloc(sizeof(WebHttpAuthUser));
    st.setString(b);
    if ((p = st.getNext()) == NULL) {
      // looks like a blank line, ignore it
      goto Labort;
    }
    if (strlen(p) > WEB_HTTP_AUTH_USER_MAX) {
      snprintf(error_msg, sizeof(error_msg), "Length of username too long, ignoring entry");
      goto Lerror;
    }
    ink_strncpy(au->user, p, WEB_HTTP_AUTH_USER_MAX);
    if ((p = st.getNext()) == NULL) {
      snprintf(error_msg, sizeof(error_msg), "Parse error, ignoring entry");
      goto Lerror;
    }
    if (strlen(p) != WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN) {
      snprintf(error_msg, sizeof(error_msg), "Malformed password, ignoring entry");
      goto Lerror;
    }
    ink_strncpy(au->encrypt_passwd, p, WEB_HTTP_AUTH_ENCRYPT_PASSWD_LEN);
    if ((p = st.getNext()) == NULL) {
      snprintf(error_msg, sizeof(error_msg), "Parse error, ignoring entry");
      goto Lerror;
    }
    au->access = atoi(p);
    if (au->access < 0 || au->access >= WEB_HTTP_AUTH_ACCESS_MODES) {
      snprintf(error_msg, sizeof(error_msg), "Invalid access mode '%d', ignoring entry", au->access);
      goto Lerror;
    }
    // check for duplicates
    if (ht->mgmt_hash_table_isbound(au->user)) {
      WebHttpAuthUser *ptr = 0;
      ht->mgmt_hash_table_lookup(au->user, (void **) &ptr);
      if (ptr) {
        xfree(ptr);
      }
      ht->mgmt_hash_table_delete(au->user);
      snprintf(error_msg, sizeof(error_msg), "Duplicate users defined, disabling user '%s'", au->user);
      // blacklist this username
      blacklist_ht->mgmt_hash_table_insert(au->user, 0);
      goto Lerror;
    } else if (blacklist_ht->mgmt_hash_table_isbound(au->user)) {
      goto Labort;
    } else {
      ht->mgmt_hash_table_insert(au->user, au);
    }
    continue;
  Lerror:
    error = true;
    mgmt_elog(stderr, "[configAuthOtherUsers] %s (file: %s) (line: %s)\n", error_msg, file, b);
  Labort:
    xfree(au);
  }

  if (error) {
    snprintf(error_msg, sizeof(error_msg), "Parse error(s) reading '%s'; some accounts may be disabled", file);
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_MGMT_CONFIG_ERROR, error_msg);
  }

  xfree(fbuf);
  if (file)
    xfree(file);
  delete blacklist_ht;

}

void
configLangDict()
{

  char fpath[FILE_NAME_MAX];
  char *fbuf;
  char *file_buf;
  int file_size;
  WebHttpContext whc;
  MgmtHashTable *ht;
  InkHashTableEntry *hte;
  InkHashTableIteratorState htis;

  Tokenizer fbuf_tok("\r\n");
  tok_iter_state fbuf_tis;
  SimpleTokenizer line_stok('=');

  const char *line;
  char *tag, *value, *value_cpy;

  // open lang dict file
  RecString file = NULL;
  int rec_err = RecGetRecordString_Xmalloc("proxy.config.admin.lang_dict", &file);
  if (rec_err != REC_ERR_OKAY)
    return;
  snprintf(fpath, FILE_NAME_MAX, "%s%s%s", mgmt_path, DIR_SEP, file);
  fbuf = 0;
  if (WebFileImport_Xmalloc(fpath, &file_buf, &file_size) != WEB_HTTP_ERR_OKAY) {
    mgmt_log(stderr, "[configLangDict] could not find language dictionary "
             "(%s); web-based user-interface may be inoperable\n", file);
    return;
  }
  // initialize an empty whc object and do <@record> substitution
  memset(&whc, 0, sizeof(WebHttpContext));
  whc.response_bdy = NEW(new textBuffer(8192));
  whc.response_hdr = NEW(new httpResponse());

  // coverity[deref_ptr_in_call]
  if (WebHttpRender(&whc, file_buf, file_size) != WEB_HTTP_ERR_OKAY) {
    mgmt_log(stderr, "[configLangDict] could not replace '<@' tags in language dictionary\n");
    return;
  }
  fbuf = whc.response_bdy->bufPtr();

  // FIXME: by empting the current hash-table element at a time, and
  // then re-populating it later, we create a small window where
  // UI language substitutions may fail.
  ht = adminContext.lang_dict_ht;
  for (hte = ht->mgmt_hash_table_iterator_first(&htis); hte != NULL; hte = ht->mgmt_hash_table_iterator_next(&htis)) {
    value = (char *) ht->mgmt_hash_table_entry_value(hte);
    xfree(value);
  }

  // contruct a new hash-table from our file
  fbuf_tok.Initialize(fbuf, SHARE_TOKS);
  for (line = fbuf_tok.iterFirst(&fbuf_tis); line; line = fbuf_tok.iterNext(&fbuf_tis)) {
    if (*line == '#')
      continue;
    line_stok.setString((char *) line);
    if ((tag = line_stok.getNext()) == NULL) {
      // looks like a blank line, ignore it
      continue;
    }
    if ((value = line_stok.getNext()) == NULL) {
      mgmt_log("[configLangDict] missing value for tag (%s) in " "dictionary (%s)", tag, file);
      continue;
    }
    // copy the string and insert into our hash_table
    value_cpy = xstrdup(value);
    ht->mgmt_hash_table_insert(tag, value_cpy);
  }

  if (file_buf)
    xfree(file_buf);
  if (file)
    xfree(file);
  if (whc.response_bdy)
    delete(whc.response_bdy);
  if (whc.response_hdr)
    delete(whc.response_hdr);
  return;

}

void
configRefreshRate()
{
  RecInt refresh;
  ink_assert(RecGetRecordInt("proxy.config.admin.ui_refresh_rate", &refresh)
             == REC_ERR_OKAY);

  // The memory write on an int is atomic so just write to the
  //   global variable
  wGlobals.refreshRate = (int) refresh;
}

void
configSSLenable()
{
  char *sslCertPath = NULL;

  RecInt sslEnabled;
  ink_assert(RecGetRecordInt("proxy.config.admin.use_ssl", &sslEnabled)
             == REC_ERR_OKAY);

  adminContext.SSLenabled = sslEnabled;

  // the SSL context
  //
  //   If we are enabling SSL with no context, then create
  //     one
  //
  //   If we are enabling SSL and there is a context,
  //     recycle the old one
  //
  //   If are disabling SSL, the old context stays around
  //      since we have no idea when current transactions
  //      are done using
  //
  if (sslEnabled > 0 && adminContext.SSL_Context == NULL) {

    // The ssl certificate is relative to the config directory
    RecString configDir = NULL;
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.config_dir", &configDir)
               == REC_ERR_OKAY);

    RecString sslCertFile = NULL;
    ink_assert(RecGetRecordString_Xmalloc("proxy.config.admin.ssl_cert_file", &sslCertFile)
               == REC_ERR_OKAY);

    if (configDir == NULL || sslCertFile == NULL) {
      mgmt_elog(stderr, "[configSSLenable] Unable to read config_dir or ssl_cert_file variable\n");
      goto SSL_FAILED;
    }

    int sizeToAllocate = strlen(configDir) + strlen(sslCertFile) + 1;
    sslCertPath = (char *) xmalloc(sizeToAllocate + 1);
    ink_strncpy(sslCertPath, configDir, sizeToAllocate);
    strncat(sslCertPath, "/", sizeToAllocate - strlen(sslCertPath));
    strncat(sslCertPath, sslCertFile, sizeToAllocate - strlen(sslCertPath));

    if (init_SSL(sslCertPath, &adminContext) < 0) {
      goto SSL_FAILED;
    }

    xfree(sslCertFile);
    xfree(sslCertPath);
    xfree(configDir);
  }

  return;

SSL_FAILED:
  const char *errMsg = "Unable to initialize SSL.  Web administration inoperable";
  mgmt_elog(stderr, "[configSSLenable] %s\n", errMsg);
  lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, errMsg);
  adminContext.SSLenabled = -1;
  xfree(sslCertPath);

  return;
}

void
configLoadFactor()
{
  computeXactMax();
}

// void configMgmtIpAllow()
//
//    Re read the Mgmt IpAllow table
//
// CALLEE MUST BE HOLDING wGlobals.serviceThrLock;
//
void
configMgmtIpAllow()
{

  if (mgmt_allow_table != NULL) {
    delete mgmt_allow_table;
  }

  mgmt_allow_table = new MgmtAllow("proxy.config.admin.ip_allow.filename", "[MgmtAllow]", "ip_allow");
  mgmt_allow_table->BuildTable();

}
