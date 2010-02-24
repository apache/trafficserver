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

#include "ink_config.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "ink_code.h"
#include "Diags.h"
#include "ParseRules.h"
#include "PluginDB.h"

/***************************************************************************
 *
 * An Inktomi Traffic Server plugin license key should look like:
 *
 *     XXXXXEEEEDDDDDD
 *
 * XXXXX is a 5 digit alphanumeric id used by plugin vendors to
 * assign to their customers.
 *
 * EEEE is the hex encoding of the expiration date. It's the number
 * of days from January 1, 1970. If a plugin has no expiration date,
 * 0000 can be used instead.
 *
 * DDDDDD is the INK_MD5 encoding of some combination of the following
 * strings: "Inktomi Traffic Server", "Plugin Name", "XXXXXEEEE".
 *
 *
 ***************************************************************************/

const char *
  PluginDB::CheckLicenseResultStr[] = {
  "license missing",
  "license expired",
  "license invalid",
  "license ok"
};

const unsigned int
  PluginDB::license_custid_len = 5;
const unsigned int
  PluginDB::license_expire_len = 4;
const unsigned int
  PluginDB::license_digest_len = 6;
const unsigned int
  PluginDB::license_total_len = PluginDB::license_custid_len +
  PluginDB::license_expire_len + PluginDB::license_digest_len;

PluginDB::PluginDB(const char *plugin_db_file)
{
  info_table = ink_hash_table_create(InkHashTableKeyType_String);
  ReadPluginDB(plugin_db_file);
}

PluginDB::~PluginDB(void)
{
  ink_hash_table_destroy_and_free_values(info_table);
}

void
PluginDB::ReadPluginDB(const char *plugin_db_file)
{
  FILE *pdb = fopen(plugin_db_file, "r");
  if (pdb == NULL) {
    Warning("unable to open plugin.db file '%s': %d, %s", plugin_db_file, errno, strerror(errno));
    return;
  }

  char line[1024];
  char plugin_obj[256];
  plugin_obj[0] = '\0';
  PluginDB::PluginInfo * pinfo = new PluginDB::PluginInfo();

  while (fgets(line, sizeof(line) - 1, pdb) != NULL) {
    char *p = line;
    while (*p && ParseRules::is_wslfcr(*p)) {
      p++;
    }
    if ((*p == '\0') || (*p == '#')) {
      continue;
    }
    // We have a non-comment and non-blank line

    // Nullify the newline character
    int len = strlen(p);
    int i;
    p[len - 1] = '\0';

    if (p[0] == '[') {
      if (plugin_obj[0] != '\0' && (pinfo->name[0] != '\0' || pinfo->license[0] != '\0')) {
        ink_hash_table_insert(info_table, (InkHashTableKey) plugin_obj, (InkHashTableValue) pinfo);
        plugin_obj[0] = '\0';
        pinfo = new PluginDB::PluginInfo();
      }
      p++;
      for (i = 0; p[i] != '\0' && p[i] != ']' && i < 255; i++) {
        pinfo->name[i] = p[i];
      }
      pinfo->name[i] = '\0';

    } else {
      if (strstr(p, "Object=")) {
        p = p + sizeof("Object=") - 1;
        for (i = 0; p[i] != '\0' && i < 255; i++) {
          plugin_obj[i] = p[i];
        }
        plugin_obj[i] = '\0';
      } else if (strstr(p, "License=")) {
        p = p + sizeof("License=") - 1;
        for (i = 0; p[i] != '\0' && i < 255; i++) {
          pinfo->license[i] = p[i];
        }
        pinfo->license[i] = '\0';
      }
    }
  }

  if (plugin_obj[0] != '\0' && (pinfo->name[0] != '\0' || pinfo->license[0] != '\0')) {
    ink_hash_table_insert(info_table, (InkHashTableKey) plugin_obj, (InkHashTableValue) pinfo);
  } else {
    delete pinfo;
  }
  fclose(pdb);
}

PluginDB::CheckLicenseResult PluginDB::CheckLicense(const char *plugin_obj)
{
  char
    buffer[1024],
    buffer_md5[16],
    buffer_md5_str[33];
  char
    expire_str[PluginDB::license_expire_len + 1];
  unsigned long
    expire_days;
  INK_DIGEST_CTX
    md5_context;
  PluginDB::PluginInfo * pinfo;
  char *
    end_ptr = NULL;

  InkHashTableEntry *
    ht_entry = ink_hash_table_lookup_entry(info_table,
                                           (InkHashTableKey) plugin_obj);
  if (ht_entry != NULL) {
    pinfo = (PluginDB::PluginInfo *) ink_hash_table_entry_value(info_table, ht_entry);
  } else {
    return PluginDB::license_missing;
  }

  if (strlen(pinfo->license) != PluginDB::license_total_len) {
    return PluginDB::license_invalid;
  }

  snprintf(buffer, sizeof(buffer), "Inktomi Traffic Server %s ", pinfo->name);
  strncat(buffer, pinfo->license, PluginDB::license_custid_len + PluginDB::license_expire_len);

  ink_code_incr_md5_init(&md5_context);
  ink_code_incr_md5_update(&md5_context, buffer, strlen(buffer));
  ink_code_incr_md5_final(buffer_md5, &md5_context);
  // coverity[uninit_use_in_call]
  ink_code_md5_stringify(buffer_md5_str, sizeof(buffer_md5_str), buffer_md5);

  if (strncmp(buffer_md5_str,
              pinfo->license + PluginDB::license_custid_len
              + PluginDB::license_expire_len, PluginDB::license_digest_len) != 0) {
    return PluginDB::license_invalid;
  }

  strncpy(expire_str, pinfo->license + PluginDB::license_custid_len, PluginDB::license_expire_len);
  expire_str[PluginDB::license_expire_len] = '\0';

  expire_days = strtoul(expire_str, &end_ptr, 16);

  if (expire_days != 0) {
    time_t
      time_now = time(NULL);
    if ((unsigned long) time_now > expire_days * (60 * 60 * 24)) {
      return PluginDB::license_expired;
    }
  }

  return PluginDB::license_ok;
}
