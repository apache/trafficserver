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

#ifndef _STAT_PROCESSOR_H_
#define _STAT_PROCESSOR_H_

/****************************************************************************
 *
 *  StatProcessor.h - Functions for computing node and cluster stat
 *                          aggregation
 *
 *
 ****************************************************************************/

#include "ts/ink_platform.h"
#include <stdarg.h>
#include "MgmtUtils.h"
#include "MgmtDefs.h"
#include "WebMgmtUtils.h"
#include "ts/ink_hrtime.h"
#include "LocalManager.h"
#include "WebOverview.h"

#include "StatType.h"

#if HAVE_LIBEXPAT
#include "expat.h"
typedef XML_Char xmlchar;
#elif HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/SAX.h>
typedef xmlChar xmlchar;
#else
#error "No XML parser - please configure expat or libxml2"
#endif

#include <string.h>
#include <stdlib.h>

class StatProcessor
{
public:
  explicit StatProcessor(FileManager *configFiles);
  ~StatProcessor();

  // Member Fuctions
  unsigned short processStat();
  void rereadConfig(FileManager *configFiles);

  LocalManager *m_lmgmt;
  overviewPage *m_overviewGenerator;
};

/**
 * External expression evaluation API.
 *
 * INPUT: an expression string, e.g.:
 * "(proxy.node.user_agent_total_bytes-proxy.node.origin_server_total_bytes)
 *  / proxy.node.user_agent_total_bytes"
 *
 * RETURN: the resulting value of the expression.
 * NOTE: it returns -9999.0 if there is an error.
 *
 */

RecData ExpressionEval(char *);

#endif
