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



#if !defined (_RECORDS_CONFIG_H_)
#define _RECORDS_CONFIG_H_

#include "MgmtDefs.h"
#include "BaseRecords.h"
#include "MgmtHashTable.h"
#include "P_RecCore.h"

enum RecordUpdateType
{
  RU_NULL,                      // default: don't know the behavior
  RU_REREAD,                    // config can be updated dynamically w/ traffic_line -x
  RU_RESTART_TS,                // config requires TS to be restarted to take effect
  RU_RESTART_TM,                // config requires TM/TS to be restarted to take effect
  RU_RESTART_TC                 // config requires TC/TM/TS to be restarted to take effect
};

enum RecordRequiredType
{
  RR_NULL,                      // config is _not_ required to be defined in records.config
  RR_REQUIRED                   // config _is_ required to be defined in record.config
};

enum RecordCheckType
{
  RC_NULL,                      // default: no check type defined
  RC_STR,                       // config is a string
  RC_INT,                       // config is an integer with a range
  RC_IP                         // config is an ip address
};

enum RecordAccessType
{
  RA_NULL,                      // default: no access type defined
  RA_NO_ACCESS,                 // config cannot be read or set
  RA_READ_ONLY                  // config can only be read
};

// Retain this struct for ease of CVS merging
struct RecordElement
{
  int type;                     // type of the record (CONFIG, PROCESS, etc)
  const char *name;             // name of the record
  const char *description;      // short description of the record
  MgmtType value_type;          // type of the record value (INT, FLOAT, etc)
  const char *value;            // default value for the record
  RecordUpdateType update;      // action necessary to change a configuration
  RecordRequiredType required;  // is records required to be in records.config?
  RecordCheckType check;
  const char *regex;
  RecordAccessType access;      // access level of the record
};

extern RecordElement RecordsConfig[];   // big struct of all system records

// remove this when librecords is done.
extern MgmtHashTable *RecordsConfigIndex;       // hash table index into record table (record_name to record_element-index)

// remove this when librecords is done.
void RecordsConfigInit();       // initializes RecordsConfigIndex

void LibRecordsConfigInit();    // initializes RecordsConfigIndex

void test_librecords();

#endif
