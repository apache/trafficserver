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

#ifndef _MGMT_DEF_H
#define _MGMT_DEF_H

/*
 * Type definitions.
 */
#include "ink_port.h"
#include "ink_hrtime.h"

typedef ink64 MgmtIntCounter;
typedef ink64 MgmtInt;
typedef ink64 MgmtLLong;
typedef float MgmtFloat;
typedef char *MgmtString;

typedef enum
{
  INVALID = -1,
  INK_INT = 0,
  INK_FLOAT = 1,
  INK_STRING = 2,
  INK_COUNTER = 3,
  INK_STAT_CONST = 4,           // Added for the StatProcessor
  INK_STAT_FX = 5,              // Added for the StatProcessor
  INK_LLONG = 6,                // Added for Long Long config options
  MAX_MGMT_TYPE = 7,
} MgmtType;

/*
 * MgmtCallback
 *   Management Callback functions.
 */
typedef void *(*MgmtCallback) (void *opaque_cb_data, char *data_raw, int data_len);

#define MGMT_SEMID_DEFAULT            11452
#define MGMT_DB_FILENAME              "mgmt_db"
#define LM_CONNECTION_SERVER          "process_server"

/* Structs used in Average Statistics calculations */
struct StatTwoIntSamples
{
  const char *lm_record_name;
  ink_hrtime previous_time;
  ink_hrtime current_time;
  MgmtInt previous_value;
  MgmtInt current_value;

  MgmtInt diff_value()
  {
    return (current_value - previous_value);
  }
  ink_hrtime diff_time()
  {
    return (current_time - previous_time);
  }
};

struct StatTwoFloatSamples
{
  const char *lm_record_name;
  ink_hrtime previous_time;
  ink_hrtime current_time;
  MgmtFloat previous_value;
  MgmtFloat current_value;

  MgmtFloat diff_value()
  {
    return (current_value - previous_value);
  }
  ink_hrtime diff_time()
  {
    return (current_time - previous_time);
  }
};

#endif /* _MGMT_DEF_H */
