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

#if !defined(_RECORDS_CONFIG_H_)
#define _RECORDS_CONFIG_H_

//#include "MgmtDefs.h"
#include "P_RecCore.h"

enum RecordRequiredType {
  RR_NULL,    // config is _not_ required to be defined in records.config
  RR_REQUIRED // config _is_ required to be defined in record.config
};

// Retain this struct for ease of CVS merging
struct RecordElement {
  RecT type;                   // type of the record (CONFIG, PROCESS, etc)
  const char *name;            // name of the record
  RecDataT value_type;         // type of the record value (INT, FLOAT, etc)
  const char *value;           // default value for the record
  RecUpdateT update;           // action necessary to change a configuration
  RecordRequiredType required; // is records required to be in records.config?
  RecCheckT check;
  const char *regex;
  RecAccessT access; // access level of the record
};

typedef void (*RecordElementCallback)(const RecordElement *, void *);
void RecordsConfigIterate(RecordElementCallback, void *);

void LibRecordsConfigInit();                 // initializes RecordsConfigIndex
void RecordsConfigOverrideFromEnvironment(); // Override records from the environment
void test_librecords();

#endif
