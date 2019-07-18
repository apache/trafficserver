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

#include "tscore/ink_config.h"
#include "RecordsConfig.h"

//-------------------------------------------------------------------------
// LibRecordsConfigInit
//-------------------------------------------------------------------------

static void
initialize_record(const RecordElement *record, void *)
{
  RecInt tempInt         = 0;
  RecFloat tempFloat     = 0.0;
  RecCounter tempCounter = 0;

  RecUpdateT update;
  RecCheckT check;
  RecAccessT access;
  RecT type;

  // Less typing ...
  type   = record->type;
  update = record->update;
  check  = record->check;
  access = record->access;

  if (REC_TYPE_IS_CONFIG(type)) {
    const char *value = RecConfigOverrideFromEnvironment(record->name, record->value);
    RecData data      = {0};
    RecSourceT source = value == record->value ? REC_SOURCE_DEFAULT : REC_SOURCE_ENV;

    // If you specify a consistency check, you have to specify a regex expression. We abort here
    // so that this breaks QA completely.
    if (record->check != RECC_NULL && record->regex == nullptr) {
      ink_fatal("%s has a consistency check but no regular expression", record->name);
    }

    RecDataSetFromString(record->value_type, &data, value);

    switch (record->value_type) {
    case RECD_INT:
      RecRegisterConfigInt(type, record->name, data.rec_int, update, check, record->regex, source, access);
      break;

    case RECD_FLOAT:
      RecRegisterConfigFloat(type, record->name, data.rec_float, update, check, record->regex, source, access);
      break;

    case RECD_STRING:
      RecRegisterConfigString(type, record->name, data.rec_string, update, check, record->regex, source, access);
      break;

    case RECD_COUNTER:
      RecRegisterConfigCounter(type, record->name, data.rec_counter, update, check, record->regex, source, access);
      break;

    default:
      ink_assert(true);
      break;
    } // switch

    RecDataZero(record->value_type, &data);
  } else { // Everything else, except PROCESS, are stats. TODO: Should modularize this too like PROCESS was done.
    ink_assert(REC_TYPE_IS_STAT(type));

    switch (record->value_type) {
    case RECD_INT:
      tempInt = (RecInt)ink_atoi64(record->value);
      RecRegisterStatInt(type, record->name, tempInt, RECP_NON_PERSISTENT);
      break;

    case RECD_FLOAT:
      tempFloat = (RecFloat)atof(record->value);
      RecRegisterStatFloat(type, record->name, tempFloat, RECP_NON_PERSISTENT);
      break;

    case RECD_STRING:
      RecRegisterStatString(type, record->name, (RecString)record->value, RECP_NON_PERSISTENT);
      break;

    case RECD_COUNTER:
      tempCounter = (RecCounter)ink_atoi64(record->value);
      RecRegisterStatCounter(type, record->name, tempCounter, RECP_NON_PERSISTENT);
      break;

    default:
      ink_assert(true);
      break;
    } // switch
  }
}

void
LibRecordsConfigInit()
{
  RecordsConfigIterate(initialize_record, nullptr);
}
