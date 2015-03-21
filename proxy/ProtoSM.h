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

   ProtoSM.h

   Description:
      Common elements for protocol state machines


 ****************************************************************************/

#ifndef _PROTO_SM_H_
#define _PROTO_SM_H_

template <class VCTentry, int max_entries> struct ProtoVCTable {
public:
  ProtoVCTable();
  VCTentry vc_table[max_entries];

  VCTentry *new_entry();
  VCTentry *find_entry(VConnection *);
  VCTentry *find_entry(VIO *);
  void remove_entry(VCTentry *);
  void cleanup_entry(VCTentry *);
  void cleanup_all();
  bool is_table_clear();
};

template <class VCTentry, int max_entries> inline ProtoVCTable<VCTentry, max_entries>::ProtoVCTable()
{
  memset(&vc_table, 0, sizeof(vc_table));
}

template <class VCTentry, int max_entries>
inline VCTentry *
ProtoVCTable<VCTentry, max_entries>::new_entry()
{
  for (int i = 0; i < max_entries; i++) {
    if (vc_table[i].vc == NULL) {
      return vc_table + i;
    }
  }

  ink_release_assert(0);
  return NULL;
}

template <class VCTentry, int max_entries>
inline VCTentry *
ProtoVCTable<VCTentry, max_entries>::find_entry(VConnection *vc)
{
  for (int i = 0; i < max_entries; i++) {
    if (vc_table[i].vc == vc) {
      return vc_table + i;
    }
  }

  return NULL;
}

template <class VCTentry, int max_entries>
inline VCTentry *
ProtoVCTable<VCTentry, max_entries>::find_entry(VIO *vio)
{
  for (int i = 0; i < max_entries; i++) {
    if (vc_table[i].read_vio == vio || vc_table[i].write_vio == vio) {
      ink_assert(vc_table[i].vc != NULL);
      return vc_table + i;
    }
  }

  return NULL;
}

// bool ProtoVCTable::remove_entry(HttpVCEntry* e)
//
//    Deallocates all buffers from the associated
//      entry and re-initializes it's other fields
//      for reuse
//
template <class VCTentry, int max_entries>
inline void
ProtoVCTable<VCTentry, max_entries>::remove_entry(VCTentry *e)
{
  ink_assert(e->vc == NULL || e->in_tunnel);
  if (e->read_buffer) {
    free_MIOBuffer(e->read_buffer);
  }
  if (e->write_buffer) {
    free_MIOBuffer(e->write_buffer);
  }
  memset(e, 0, sizeof(VCTentry));
}

// void ProtoVCTable::cleanup_entry(HttpVCEntry* e)
//
//    Closes the associate vc for the entry,
//     and the call remove_entry
//
template <class VCTentry, int max_entries>
inline void
ProtoVCTable<VCTentry, max_entries>::cleanup_entry(VCTentry *e)
{
  ink_assert(e->vc);
  if (e->in_tunnel == false) {
    e->vc->do_io_close();
    e->vc = NULL;
  }
  remove_entry(e);
}

template <class VCTentry, int max_entries>
inline void
ProtoVCTable<VCTentry, max_entries>::cleanup_all()
{
  for (int i = 0; i < max_entries; i++) {
    if (vc_table[i].vc != NULL) {
      cleanup_entry(vc_table + i);
    }
  }
}


template <class VCTentry, int max_entries>
inline bool
ProtoVCTable<VCTentry, max_entries>::is_table_clear()
{
  for (int i = 0; i < max_entries; i++) {
    if (vc_table[i].vc != NULL) {
      return false;
    }
  }
  return true;
}

#endif
