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

#ifndef __MGMT_SCHEMA_
#define _MGMT_SCHEMA_

#include "ink_bool.h"

/****************************************************************************
 *
 *  MgmtSchema.h - Functions for interfacing to manage Traffic Server Schema
 *
 *
 *
 ****************************************************************************/

#include "ink_hash_table.h"
#include "XmlUtils.h"

#include "P_RecCore.h"

#define SCHM_UpdateT       RecUpdateT
#define SCHM_RU_NULL       RECU_NULL
#define SCHM_RU_REREAD     RECU_DYNAMIC
#define SCHM_RU_RESTART_TS RECU_RESTART_TS
#define SCHM_RU_RESTART_TM RECU_RESTART_TM
#define SCHM_RU_RESTART_TC RECU_RESTART_TC

#define SCHM_CheckT        RecCheckT
#define SCHM_RC_NULL       RECC_NULL
#define SCHM_RC_STR        RECC_STR
#define SCHM_RC_INT        RECC_INT
#define SCHM_RC_IP         RECC_IP


bool validateRecordsConfig(XMLNode * node);
bool validateNode(XMLNode * node);

#endif // _MGMT_SCHEMA
