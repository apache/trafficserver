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

/*****************************************************************************
 *
 *  HostSelection.h - Interface to Host Selection System
 *
 *
 ****************************************************************************/

#ifndef _HOST_SELECTION_H_
#define _HOST_SELECTION_H_

#include "ControlBase.h"
#include "ControlMatcher.h"
#include "P_RecProcess.h"

enum HostStatus_t {
  HOST_STATUS_INIT,
  HOST_STATUS_UP,
  HOST_STATUS_DOWN,
};

/**
 * Singleton placeholder for next hop status.
 */
struct HostStatus {
  ~HostStatus();

  static HostStatus &
  instance()
  {
    static HostStatus instance;
    return instance;
  } // return the signleton pointer.
  void setHostStatus(const char *key, const HostStatus_t status);
  HostStatus_t getHostStatus(const char *key);

private:
  HostStatus();
  HostStatus(const HostStatus &obj) = delete;
  HostStatus &operator=(HostStatus const &) = delete;

  InkHashTable *hosts_statuses; // next hop status, key is hostname or ip string, data is bool (available).
  ink_mutex hosts_statuses_mutex;
};

#endif
