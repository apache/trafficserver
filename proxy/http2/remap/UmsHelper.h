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

#ifndef _UMS_HELPER_H_
#define _UMS_HELPER_H_

#include "UrlMapping.h"
#include "StringHash.h"

class url_mapping;

/**
 * Used to store addtional information for fast search in UrlRewrite::TableLookup
**/
class ums_helper
{
public:
  url_mapping * empty_list;
  url_mapping *unique_list;
  StringHash *hash_table;
  int min_path_size;
  int max_path_size;
  int map_cnt;
  bool tag_present;
    ums_helper();
   ~ums_helper();
  StringHash *init_hash_table(int _map_cnt = (-1));
  void delete_hash_table(void);
  int load_hash_table(url_mapping * list);
  url_mapping *lookup_best_empty(const char *request_host, int request_port, char *tag);
  url_mapping *lookup_best_notempty(url_mapping * ht_entry,
                                    const char *request_host, int request_port,
                                    const char *request_path, int request_path_len, char *tag);
};

#endif
