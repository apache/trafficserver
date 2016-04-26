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
 * Filename: CfgContextManager.h (based on FileOp.h)
 * ------------------------------------------------------------------------
 * Purpose:
 *    Implements some of TSCfgContext functionality in the API; deals only
 *    with the CfgContext class though, not the TSCfgContext; typically
 *    the TSCfgContext function is a wrapper for the function whose purpose
 *    is to make the appropriate typecast to a CfgContext*
 *
 ***************************************************************************/

#ifndef _CFG_CONTEXT_MANAGER_H
#define _CFG_CONTEXT_MANAGER_H

#include "mgmtapi.h"
#include "CfgContextImpl.h"
#include "CfgContextDefs.h"

/***************************************************************************
 * CfgContext Operations
 ***************************************************************************/
/* based on file to change, dequeues the add/rm rule, and translates it
 * into CoreAPI terms; because the file name stored , knows the format
 * of the rules in the queue
 */
CfgContext *CfgContextCreate(TSFileNameT filetype);
TSMgmtError CfgContextDestroy(CfgContext *ctx);
TSMgmtError CfgContextCommit(CfgContext *ctx, LLQ *errRules = NULL);
TSMgmtError CfgContextGet(CfgContext *ctx);

/***************************************************************************
 * CfgContext Operations
 ***************************************************************************/
/* returns number of ele's in the CfgContext * */
int CfgContextGetCount(CfgContext *ctx);

/* user must typecast the TSCfgEle to appropriate TSEle before using */
TSCfgEle *CfgContextGetEleAt(CfgContext *ctx, int index);
CfgEleObj *CfgContextGetObjAt(CfgContext *ctx, int index);

TSCfgEle *CfgContextGetFirst(CfgContext *ctx, TSCfgIterState *state);
TSCfgEle *CfgContextGetNext(CfgContext *ctx, TSCfgIterState *state);

TSMgmtError CfgContextMoveEleUp(CfgContext *ctx, int index);
TSMgmtError CfgContextMoveEleDown(CfgContext *ctx, int index);

TSMgmtError CfgContextAppendEle(CfgContext *ctx, TSCfgEle *ele);
TSMgmtError CfgContextInsertEleAt(CfgContext *ctx, TSCfgEle *ele, int index);
TSMgmtError CfgContextRemoveEleAt(CfgContext *ctx, int index);
TSMgmtError CfgContextRemoveAll(CfgContext *ctx);

#endif
