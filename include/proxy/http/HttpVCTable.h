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

#pragma once

#include "IOBuffer.h"
#include "VConnection.h"
#include "VIO.h"

class HttpSM;
using HttpSMHandler = int (HttpSM::*)(int, void *);

enum HttpVC_t {
  HTTP_UNKNOWN = 0,
  HTTP_UA_VC,
  HTTP_SERVER_VC,
  HTTP_TRANSFORM_VC,
  HTTP_CACHE_READ_VC,
  HTTP_CACHE_WRITE_VC,
  HTTP_RAW_SERVER_VC
};

struct HttpVCTableEntry {
  VConnection *vc;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio;
  VIO *write_vio;
  HttpSMHandler vc_read_handler;
  HttpSMHandler vc_write_handler;
  HttpVC_t vc_type;
  HttpSM *sm;
  bool eos;
  bool in_tunnel;
};

struct HttpVCTable {
  static const int vc_table_max_entries = 4;
  explicit HttpVCTable(HttpSM *);

  HttpVCTableEntry *new_entry();
  HttpVCTableEntry *find_entry(VConnection *);
  HttpVCTableEntry *find_entry(VIO *);
  void remove_entry(HttpVCTableEntry *);
  void cleanup_entry(HttpVCTableEntry *);
  void cleanup_all();
  bool is_table_clear() const;

private:
  HttpVCTableEntry vc_table[vc_table_max_entries];
  HttpSM *sm = nullptr;
};

inline bool
HttpVCTable::is_table_clear() const
{
  for (const auto &i : vc_table) {
    if (i.vc != nullptr) {
      return false;
    }
  }
  return true;
}
