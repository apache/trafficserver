/** @file

  Implementation of Host Proxy routing

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
#include "HostStatus.h"

HostStatus::HostStatus() : hosts_statuses(ink_hash_table_create(InkHashTableKeyType_String))
{
  ink_mutex_init(&hosts_statuses_mutex);
}

HostStatus::~HostStatus()
{
  ink_hash_table_destroy(hosts_statuses);

  ink_mutex_destroy(&hosts_statuses_mutex);
}

void
HostStatus::setHostStatus(const char *key, HostStatus_t status)
{
  Debug("host_statuses", "HostStatus::setHostStatus():  key: %s, status: %d", key, status);
  ink_mutex_acquire(&hosts_statuses_mutex);
  // update / insert status.
  // using the hash table pointer to store the HostStatus_t value.
  ink_hash_table_insert(hosts_statuses, key, reinterpret_cast<void *>(status));

  ink_mutex_release(&hosts_statuses_mutex);
}

HostStatus_t
HostStatus::getHostStatus(const char *key)
{
  intptr_t _status = HostStatus_t::HOST_STATUS_INIT;

  // the hash table value pointer has the HostStatus_t value.
  ink_hash_table_lookup(hosts_statuses, key, reinterpret_cast<void **>(&_status));
  Debug("host_statuses", "HostStatus::getHostStatus():  key: %s, status: %d", key, static_cast<int>(_status));

  return static_cast<HostStatus_t>(_status);
}
