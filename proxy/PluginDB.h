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

#ifndef __PLUGIN_DB_H__
#define __PLUGIN_DB_H__

#include "ink_hash_table.h"

class PluginDB
{
public:
  typedef enum
  {
    license_missing = 0,
    license_expired,
    license_invalid,
    license_ok
  } CheckLicenseResult;

    PluginDB(const char *plugin_db_file);
   ~PluginDB(void);

  CheckLicenseResult CheckLicense(const char *plugin_obj);

  static const char *CheckLicenseResultStr[];

private:

  typedef struct
  {
    char name[256];
    char license[256];
  } PluginInfo;

  static const unsigned int license_custid_len;
  static const unsigned int license_expire_len;
  static const unsigned int license_digest_len;
  static const unsigned int license_total_len;

  void ReadPluginDB(const char *plugin_db_file);

  InkHashTable *info_table;
};

#endif /* __PLUGIN_DB_H__ */
