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

#include "proxy/http/HttpConfig.h"
#include "proxy/http/HttpVCTable.h"

#include "iocore/eventsystem/VConnection.h"
#include "iocore/eventsystem/VIO.h"
#include "tscore/ink_assert.h"

class HttpSM;

HttpVCTable::HttpVCTable(HttpSM *mysm)
{
  memset(&vc_table, 0, sizeof(vc_table));
  sm = mysm;
}

HttpVCTableEntry *
HttpVCTable::new_entry()
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc == nullptr) {
      vc_table[i].sm = sm;
      return vc_table + i;
    }
  }

  ink_release_assert(0);
  return nullptr;
}

HttpVCTableEntry *
HttpVCTable::find_entry(VConnection *vc)
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc == vc) {
      return vc_table + i;
    }
  }

  return nullptr;
}

HttpVCTableEntry *
HttpVCTable::find_entry(VIO *vio)
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].read_vio == vio || vc_table[i].write_vio == vio) {
      ink_assert(vc_table[i].vc != nullptr);
      return vc_table + i;
    }
  }

  return nullptr;
}

// bool HttpVCTable::remove_entry(HttpVCEntry* e)
//
//    Deallocates all buffers from the associated
//      entry and re-initializes it's other fields
//      for reuse
//
void
HttpVCTable::remove_entry(HttpVCTableEntry *e)
{
  e->vc  = nullptr;
  e->eos = false;
  if (e->read_buffer) {
    free_MIOBuffer(e->read_buffer);
    e->read_buffer = nullptr;
  }
  if (e->write_buffer) {
    free_MIOBuffer(e->write_buffer);
    e->write_buffer = nullptr;
  }
  // Cannot reach in to checkout the netvc
  // for remaining I/O operations because the netvc
  // may have been deleted at this point and the pointer
  // could be stale.
  e->read_vio         = nullptr;
  e->write_vio        = nullptr;
  e->vc_read_handler  = nullptr;
  e->vc_write_handler = nullptr;
  e->vc_type          = HttpVC_t::UNKNOWN;
  e->in_tunnel        = false;
}

// bool HttpVCTable::cleanup_entry(HttpVCEntry* e)
//
//    Closes the associate vc for the entry,
//     and the call remove_entry
//
void
HttpVCTable::cleanup_entry(HttpVCTableEntry *e)
{
  ink_assert(e->vc);
  if (e->in_tunnel == false) {
    if (e->vc_type == HttpVC_t::SERVER_VC) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_cleanup_entry);
    }
    e->vc->do_io_close();
    e->vc = nullptr;
  }
  remove_entry(e);
}

void
HttpVCTable::cleanup_all()
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc != nullptr) {
      cleanup_entry(vc_table + i);
    }
  }
}
