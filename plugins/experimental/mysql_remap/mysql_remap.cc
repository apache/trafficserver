/*
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

#include <ts/ts.h>
#include <ts/remap.h>
#include <cstdio>
#include <unistd.h>

#include "mysql/mysql.h"
#include "lib/iniparser.h"
#include "default.h"

MYSQL mysql;

typedef struct {
  char *query;
} my_data;

bool
do_mysql_remap(TSCont contp, TSHttpTxn txnp)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc, url_loc, field_loc;
  bool ret_val = false;

  const char *request_host;
  int request_host_length = 0;
  const char *request_scheme;
  int request_scheme_length = 0;
  int request_port          = 80;
  char *query;

  MYSQL_ROW row;
  MYSQL_RES *res;

  my_data *data = (my_data *)TSContDataGet(contp);
  query         = data->query;

  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "could not get request data");
    return false;
  }

  TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc);

  if (!url_loc) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request url");
    goto release_hdr;
  }

  field_loc = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);

  if (!field_loc) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve request HOST header");
    goto release_url;
  }

  request_host = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, field_loc, -1, &request_host_length);
  if (!request_host_length) {
    TSDebug(PLUGIN_NAME, "couldn't find request HOST header");
    goto release_field;
  }

  request_scheme = TSUrlSchemeGet(reqp, url_loc, &request_scheme_length);
  request_port   = TSUrlPortGet(reqp, url_loc);

  TSDebug(PLUGIN_NAME, "      +++++MYSQL REMAP+++++      ");

  TSDebug(PLUGIN_NAME, "\nINCOMING REQUEST ->\n ::: from_scheme_desc: %.*s\n ::: from_hostname: %.*s\n ::: from_port: %d",
          request_scheme_length, request_scheme, request_host_length, request_host, request_port);

  snprintf(query, QSIZE, " \
    SELECT \
        t_scheme.scheme_desc, \
        t_host.hostname, \
        to_port \
      FROM map \
        INNER JOIN scheme as t_scheme ON (map.to_scheme_id = t_scheme.id) \
        INNER JOIN scheme as f_scheme ON (map.from_scheme_id = f_scheme.id) \
        INNER JOIN hostname as t_host ON (map.to_hostname_id = t_host.id) \
        INNER JOIN hostname as f_host ON (map.from_hostname_id = f_host.id) \
      WHERE \
        is_enabled=1 \
        AND f_host.hostname = '%.*s' \
        AND f_scheme.id = %d \
        AND from_port = %d \
      LIMIT 1",
           request_host_length, request_host, (strcmp(request_scheme, "https") == 0) ? 2 : 1, request_port);

  mysql_real_query(&mysql, query, (unsigned int)strlen(query));
  res = mysql_use_result(&mysql);

  if (!res)
    goto not_found; // TODO: define a fallback

  do {
    row = mysql_fetch_row(res);
    if (!row)
      goto not_found;
    TSDebug(PLUGIN_NAME, "\nOUTGOING REQUEST ->\n ::: to_scheme_desc: %s\n ::: to_hostname: %s\n ::: to_port: %s", row[0], row[1],
            row[2]);
    TSMimeHdrFieldValueStringSet(reqp, hdr_loc, field_loc, 0, row[1], -1);
    TSUrlHostSet(reqp, url_loc, row[1], -1);
    TSUrlSchemeSet(reqp, url_loc, row[0], -1);
    TSUrlPortSet(reqp, url_loc, atoi(row[2]));
  } while (false);

  ret_val = true;

not_found:
  if (!ret_val) {
    // lets build up a nice 404 message for someone
    TSHttpHdrStatusSet(reqp, hdr_loc, TS_HTTP_STATUS_NOT_FOUND);
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_NOT_FOUND);
  }
  if (res)
    mysql_free_result(res);
#if (TS_VERSION_NUMBER < 2001005)
  if (request_host)
    TSHandleStringRelease(reqp, hdr_loc, request_host);
  if (request_scheme)
    TSHandleStringRelease(reqp, hdr_loc, request_scheme);
#endif
release_field:
  if (field_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, field_loc);
  }
release_url:
  if (url_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
release_hdr:
  if (hdr_loc) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  return ret_val;
}

static int
mysql_remap(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp   = (TSHttpTxn)edata;
  TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(PLUGIN_NAME, "Reading Request");
    TSSkipRemappingSet(txnp, 1);
    if (!do_mysql_remap(contp, txnp)) {
      reenable = TS_EVENT_HTTP_ERROR;
    }
    break;
  default:
    break;
  }

  TSHttpTxnReenable(txnp, reenable);
  return 1;
}

void
TSPluginInit(int argc, const char *argv[])
{
  dictionary *ini;
  const char *host;
  int port;
  const char *username;
  const char *password;
  const char *db;

  my_data *data = (my_data *)malloc(1 * sizeof(my_data));

  TSPluginRegistrationInfo info;
  bool reconnect = true;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[mysql_remap] Plugin registration failed");
  }

  if (argc != 2) {
    TSError("[mysql_remap] Usage: %s /path/to/sample.ini", argv[0]);
    return;
  }

  ini = iniparser_load(argv[1]);
  if (!ini) {
    TSError("[mysql_remap] Error with ini file (1)");
    TSDebug(PLUGIN_NAME, "Error parsing ini file(1)");
    return;
  }

  host     = iniparser_getstring(ini, "mysql_remap:mysql_host", (char *)"localhost");
  port     = iniparser_getint(ini, "mysql_remap:mysql_port", 3306);
  username = iniparser_getstring(ini, "mysql_remap:mysql_username", nullptr);
  password = iniparser_getstring(ini, "mysql_remap:mysql_password", nullptr);
  db       = iniparser_getstring(ini, "mysql_remap:mysql_database", (char *)"mysql_remap");

  if (mysql_library_init(0, NULL, NULL)) {
    TSError("[mysql_remap] Error initializing mysql client library");
    TSDebug(PLUGIN_NAME, "Error initializing mysql client library");
    return;
  }

  if (!mysql_init(&mysql)) {
    TSError("[mysql_remap] Could not initialize MySQL");
    TSDebug(PLUGIN_NAME, "Could not initialize MySQL");
    return;
  }

  mysql_options(&mysql, MYSQL_OPT_RECONNECT, &reconnect);

  if (!mysql_real_connect(&mysql, host, username, password, db, port, NULL, 0)) {
    TSError("[mysql_remap] Could not connect to mysql");
    TSDebug(PLUGIN_NAME, "Could not connect to mysql: %s", mysql_error(&mysql));
    return;
  }

  data->query = (char *)TSmalloc(QSIZE * sizeof(char)); // TODO: malloc smarter sizes

  TSDebug(PLUGIN_NAME, "h: %s; u: %s; p: %s; p:%d; d:%s", host, username, password, port, db);
  TSCont cont = TSContCreate(mysql_remap, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);

  TSContDataSet(cont, (void *)data);

  TSDebug(PLUGIN_NAME, "plugin is successfully initialized [plugin mode]");
  iniparser_freedict(ini);
  return;
}
