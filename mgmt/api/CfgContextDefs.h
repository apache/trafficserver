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

/***************************************************************************
 * Type Definitions
 ***************************************************************************/
#ifndef _MGMT_API_DEFS_
#define _MGMT_API_DEFS_

#include "mgmtapi.h"
#include "ts/ink_llqueue.h"

// for buffer used temporarily to parse incoming commands.
#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 4098
#endif

#ifndef MAX_FILE_SIZE
#define MAX_FILE_SIZE 4098
#endif

#ifndef MAX_RULE_SIZE
#define MAX_RULE_SIZE 1024
#endif

#define NUM_SEC_SPECS 8

#define DELIMITER '#'
#define DELIMITER_STR "#"
#define RANGE_DELIMITER '-'
#define RANGE_DELIMITER_STR "- "
#define CIDR_DELIMITER '/'
#define CIDR_DELIMITER_STR "/"
#define IP_END_DELIMITER "#"
#define LIST_DELIMITER ", "

/* Each opaque List handle in the mgmtapi should have
 * a corresponding typedef here. Using LLQ's to implement the
 * lists
 */
typedef LLQ PortList;
typedef LLQ IpAddrList;
typedef LLQ DomainList;
typedef LLQ StringList;

/* INKCommentEle only used internally by the CfgContext */
/* should this be defined in CfgContextUtils.h ?? */
typedef struct {
  TSCfgEle cfg_ele;
  char *comment;
} INKCommentEle;

#endif
