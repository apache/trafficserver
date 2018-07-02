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

/*
 *
 * MgmtDef.h
 *   Some mgmt definitions for relatively general use.
 *
 * $Date: 2006-03-08 19:40:20 $
 *
 *
 */

#pragma once

/*
 * Type definitions.
 */
#include "ts/ink_defs.h"
//#include "ts/ink_hrtime.h"

typedef int64_t MgmtIntCounter;
typedef int64_t MgmtInt;
typedef int8_t MgmtByte;
typedef float MgmtFloat;
typedef char *MgmtString;

typedef enum {
  MGMT_INVALID  = -1,
  MGMT_INT      = 0,
  MGMT_FLOAT    = 1,
  MGMT_STRING   = 2,
  MGMT_COUNTER  = 3,
  MGMT_TYPE_MAX = 4,
} MgmtType;

/*
 * MgmtCallback
 *   Management Callback functions.
 */
typedef void *(*MgmtCallback)(void *opaque_cb_data, char *data_raw, int data_len);

#define LM_CONNECTION_SERVER "processerver.sock"
