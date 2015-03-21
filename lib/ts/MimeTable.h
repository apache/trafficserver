/** @file

  MimeTableEntry and MimeTable declarations

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

#if !defined(_MimeTable_h_)
#define _MimeTable_h_

#include <string.h>
#include "ink_defs.h"
#include "ink_string.h"

struct MimeTableEntry {
  const char *name;
  const char *mime_type;
  const char *mime_encoding;
  const char *icon;

  friend int operator==(const MimeTableEntry &a, const MimeTableEntry &b) { return (strcasecmp(a.name, b.name) == 0); }
  friend int operator<(const MimeTableEntry &a, const MimeTableEntry &b) { return (strcasecmp(a.name, b.name) < 0); }
  friend int operator>(const MimeTableEntry &a, const MimeTableEntry &b) { return (strcasecmp(a.name, b.name) < 0); }
};

class MimeTable
{
public:
  MimeTable() {}
  ~MimeTable() {}

  MimeTableEntry *get_entry_path(const char *path);
  MimeTableEntry *get_entry(const char *name);

private:
  static MimeTableEntry m_table[];
  static int m_table_size;
  static MimeTableEntry m_unknown;

private:
  MimeTable(const MimeTable &);
  MimeTable &operator=(const MimeTable &);
};
extern MimeTable mimeTable;
#endif
