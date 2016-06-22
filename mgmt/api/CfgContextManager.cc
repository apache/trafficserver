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
 * Filename: ContextManager.cc
 * ------------------------------------------------------------------------
 * Purpose:
 * 1) CfgContext class manipulations
 * 2) conversions from parser value tags into Ele value format
 *
 * Created by: Lan Tran
 *
 ***************************************************************************/
#include "ts/ink_platform.h"
#include "ts/ink_string.h"
#include "CfgContextManager.h"
#include "CfgContextUtils.h"
#include "CoreAPI.h"
#include "GenericParser.h"

//--------------------------------------------------------------------------
// Defines
//--------------------------------------------------------------------------

#define FORMAT_TO_RULE_ERROR "# ERROR: Can't convert Ele to rule format."

/* ---------------------------------------------------------------
 * CfgContextCreate
 * ---------------------------------------------------------------
 * Allocates memory for a CfgContext struct and initializes its members.
 * Must pass in a valid file name type; TS_FNAME_UNDEFINED is
 * considered valid.
 */
CfgContext *
CfgContextCreate(TSFileNameT file)
{
  CfgContext *ctx = NULL;
  ctx             = new CfgContext(file);
  return ctx;
}

/* ---------------------------------------------------------------
 * CfgContextDestroy
 * ---------------------------------------------------------------
 * frees all memory allocated for the CfgContext
 */
TSMgmtError
CfgContextDestroy(CfgContext *ctx)
{
  if (!ctx)
    return TS_ERR_PARAMS;
  delete ctx;
  return TS_ERR_OKAY;
}

/* ---------------------------------------------------------------
 * CfgContextCommit
 * ---------------------------------------------------------------
 * Convert CfgContext into file, and write the file.
 * Return TS_ERR_FAIL if completely fail to commit changes (eg.
 * write new file). Returns TS_ERR_INVALID_CONFIG_RULE if succeeded
 * in commiting changes, but at least one rule was invalid.
 * errRules is an optional argument - if specified, then a
 * list of the indices of the invalid rules will be stored in it
 */
TSMgmtError
CfgContextCommit(CfgContext *ctx, LLQ *errRules)
{
  int ret;
  char *new_text = NULL;
  char *rule     = NULL;
  CfgEleObj *ele;
  int ver, size = 0, index;
  int *iPtr;
  TSMgmtError err   = TS_ERR_OKAY;
  int max_file_size = MAX_FILE_SIZE;
  int len           = 0;

  ink_assert(ctx);
  if (!ctx)
    return TS_ERR_PARAMS;

  new_text    = (char *)ats_malloc(max_file_size + 1);
  new_text[0] = '\0';
  ele         = ctx->first();
  index       = 0;
  while (ele) {
    rule = ele->formatEleToRule(); // use polymorphism
    if (!rule) {
      err  = TS_ERR_INVALID_CONFIG_RULE;
      rule = ats_strdup(FORMAT_TO_RULE_ERROR);
      if (errRules) {
        iPtr  = (int *)ats_malloc(sizeof(int));
        *iPtr = index;
        enqueue(errRules, (void *)iPtr);
      }
    }
    // write the rule to end of the file
    len = strlen(rule);
    size += len + 1;
    if (size > max_file_size) {
      max_file_size *= 2;
      new_text = (char *)ats_realloc(new_text, max_file_size + 1);
    }
    ink_strlcat(new_text, rule, max_file_size + 1);
    ink_strlcat(new_text, "\n", max_file_size + 1);

    ats_free(rule);
    if (ele->getRuleType() != TS_TYPE_COMMENT)
      index++;
    ele = ctx->next(ele);
  }

  // commit new file
  ver = ctx->getVersion();
  ret = WriteFile(ctx->getFilename(), new_text, size, ver);
  ats_free(new_text);
  if (ret != TS_ERR_OKAY)
    return TS_ERR_FAIL; // couldn't write file

  return err;
}

/* ---------------------------------------------------------------
 * CfgContextGet
 * ---------------------------------------------------------------
 * Read the file, get a copy of it, parse the file, convert the
 * parse structure into ones which will be stored by ctx. The
 * ctx should also store the version number of the file being read.
 * If invalid rule, it is skipped and not added to the CfgContext.
 */
TSMgmtError
CfgContextGet(CfgContext *ctx)
{
  TSMgmtError ret;
  int size, ver;
  char *old_text      = NULL;
  RuleList *rule_list = NULL;
  CfgEleObj *ele      = NULL;

  ink_assert(ctx);
  if (!ctx)
    return TS_ERR_PARAMS;

  // get copy of the file
  ret = ReadFile(ctx->getFilename(), &old_text, &size, &ver);
  if (ret != TS_ERR_OKAY) {
    // TODO: Hmmm, this looks almost like a memory leak, why the strcmp ??
    if (old_text && strcmp(old_text, "") != 0)
      ats_free(old_text); // need to free memory
    return ret;           // Pass the error code along
  }
  // store version number
  ctx->setVersion(ver);

  rule_list = new RuleList();
  rule_list->parse(old_text, (ctx->getFilename()));

  // iterate through each TokenList in rule_list
  for (Rule *rule_node = rule_list->first(); rule_node; rule_node = rule_list->next(rule_node)) {
    // rule_node->Print();
    ele = create_ele_obj_from_rule_node(rule_node);
    if (ele == NULL) { // invalid rule; skip it
      continue;
    }

    ret = ctx->addEle(ele);
    if (ret != TS_ERR_OKAY) {
      ats_free(old_text); // need to free memory
      return ret;
    }
  }
  delete (rule_list); // free RuleList memory
  // TODO: Hmmm, this looks almost like a memory leak, why the strcmp ??
  if (old_text && strcmp(old_text, "") != 0)
    ats_free(old_text); // need to free memory
  return TS_ERR_OKAY;
}

/***************************************************************
 * CfgContext Operations
 ***************************************************************/
/*--------------------------------------------------------------
 * CfgContextGetCount
 *--------------------------------------------------------------
 * returns number of (non-comment) ele's in the CfgContext*;
 * returns -1 if an error
 */
int
CfgContextGetCount(CfgContext *ctx)
{
  CfgEleObj *curr_ele;
  int count = 0;

  ink_assert(ctx);
  if (!ctx)
    return -1;

  // iterate CfgContext; return the first EleObj that's not a comment
  curr_ele = ctx->first(); // get head of ele
  while (curr_ele) {
    if (curr_ele->getRuleType() != TS_TYPE_COMMENT) {
      count++; // only count non-comments
    }
    curr_ele = ctx->next(curr_ele);
  }

  return count;
}

/*--------------------------------------------------------------
 * CfgContextGetObjAt
 *--------------------------------------------------------------
 * user must typecast the TSCfgObj to appropriate TSObj before using;
 * iterate through the CfgContext, until the count of NON-Comment ObjObj's
 * equals index specified (remember to start counting from 0)
 */
CfgEleObj *
CfgContextGetObjAt(CfgContext *ctx, int index)
{
  CfgEleObj *curr_ele;
  int count = 0; // start counting from 0

  ink_assert(ctx);
  if (!ctx)
    return NULL;

  // iterate through the ctx, keep count of all NON-Comment Obj Objects
  curr_ele = ctx->first(); // get head of ele
  while (curr_ele) {
    if (curr_ele->getRuleType() == TS_TYPE_COMMENT) { /* a comment ele */
      curr_ele = ctx->next(curr_ele);
      continue;
    } else {
      if (count == index) { // got right ele
        return curr_ele;
      }
      curr_ele = ctx->next(curr_ele);
      count++;
    }
  }

  return NULL; // invalid index
}

/*--------------------------------------------------------------
 * CfgContextGetEleAt
 *--------------------------------------------------------------
 * user must typecast the TSCfgEle to appropriate TSEle before using;
 * iterate through the CfgContext, until the count of NON-Comment EleObj's
 * equals index specified (remember to start counting from 0)
 */
TSCfgEle *
CfgContextGetEleAt(CfgContext *ctx, int index)
{
  TSCfgEle *cfg_ele;
  CfgEleObj *curr_ele;
  int count = 0; // start counting from 0

  ink_assert(ctx);
  if (!ctx)
    return NULL;

  // iterate through the ctx, keep count of all NON-Comment Ele Objects
  curr_ele = ctx->first(); // get head of ele
  while (curr_ele) {
    if (curr_ele->getRuleType() == TS_TYPE_COMMENT) { /* a comment ele */
      curr_ele = ctx->next(curr_ele);
      continue;
    } else {
      if (count == index) { // got right ele
        // Debug("config", "Get ele at index = %d\n", index);
        cfg_ele = curr_ele->getCfgEle();
        return cfg_ele;
      }
      curr_ele = ctx->next(curr_ele);
      count++;
    }
  }

  return NULL; // invalid index
}

/*--------------------------------------------------------------
 * CfgContextGetFirst
 *--------------------------------------------------------------
 * Returns pointer to first Non-comment Ele in the CfgContext.
 * Used as part of iterator
 */
TSCfgEle *
CfgContextGetFirst(CfgContext *ctx, TSCfgIterState *state)
{
  CfgEleObj *curr_ele;

  ink_assert(ctx && state);
  if (!ctx || !state)
    return NULL;

  // iterate; return the first CfgEleObj that's not a comment
  curr_ele = ctx->first(); // get head of ele
  while (curr_ele) {
    if (curr_ele->getRuleType() == TS_TYPE_COMMENT) { // a comment ele
      curr_ele = ctx->next(curr_ele);
      continue;
    } else {
      *state = curr_ele;
      return (curr_ele->getCfgEle());
    }
  }

  return NULL;
}

/*--------------------------------------------------------------
 * CfgContextGetNext
 *--------------------------------------------------------------
 * Returns pointer to next Non-comment Ele in the CfgContext.
 * Used as part of iterator.
 */
TSCfgEle *
CfgContextGetNext(CfgContext *ctx, TSCfgIterState *state)
{
  CfgEleObj *curr_ele;

  ink_assert(ctx && state);
  if (!ctx || !state)
    return NULL;

  // iterate through the ctx, keep count of all NON-Comment Ele Objects
  // when count == state->index, then return next ele
  curr_ele = (CfgEleObj *)*state;
  curr_ele = ctx->next(curr_ele); // get next ele
  while (curr_ele) {
    if (curr_ele->getRuleType() != TS_TYPE_COMMENT) { // a non-comment ele
      *state = curr_ele;
      return curr_ele->getCfgEle();
    }
    curr_ele = ctx->next(curr_ele); // get next ele

    /*
       if (curr_ele->getRuleType() == TS_TYPE_COMMENT) { // a comment ele
       continue;
       } else {
       *state = curr_ele;
       return curr_ele->getCfgEle();
       }
       curr_ele = ctx->next(curr_ele); // get next ele
     */
  }

  return NULL; // ERROR
}

/*--------------------------------------------------------------
 * CfgContextMoveEleUp
 *--------------------------------------------------------------
 * Remove the EleObj at the specified index (but make a copy of it's m_ele
 * first), and insert the copy at the position of index-1.
 * THIS IS REALLY INEFFICIENT!!!
 */
TSMgmtError
CfgContextMoveEleUp(CfgContext *ctx, int index)
{
  CfgEleObj *curr_ele_obj;
  TSCfgEle *ele_copy = 0; /* lv: just to make gcc happy */
  int count          = 0; // start counting from 0
  TSMgmtError ret;

  ink_assert(ctx && index >= 0);
  if (!ctx || index < 0)
    return TS_ERR_PARAMS;

  // moving the first Ele up does nothing
  if (index == 0)
    return TS_ERR_OKAY;

  // retrieve the ELe and make a copy of it
  curr_ele_obj = ctx->first(); // get head of ele
  while (curr_ele_obj) {
    if (curr_ele_obj->getRuleType() == TS_TYPE_COMMENT) { // a comment ele
      curr_ele_obj = ctx->next(curr_ele_obj);
      continue;
    } else {
      if (count == index) {
        // make a copy of ele
        ele_copy = curr_ele_obj->getCfgEleCopy();

        // remove the ele
        ctx->removeEle(curr_ele_obj);
        break;
      }
      curr_ele_obj = ctx->next(curr_ele_obj);
      count++;
    }
  }
  // reached end of CfgContext before hit index
  if (count != index)
    return TS_ERR_FAIL;

  // reinsert  the ele at index-1
  ret = CfgContextInsertEleAt(ctx, ele_copy, index - 1);

  return ret;
}

/*--------------------------------------------------------------
 * CfgContextMoveEleDown
 *--------------------------------------------------------------
 * Locate the EleObj at position index. Remove and delete the EleObj.
 * Make a copy of the ele stored in the EleObj before deleting it and
 * reinserts the new EleObj in the index+1 position.
 */
TSMgmtError
CfgContextMoveEleDown(CfgContext *ctx, int index)
{
  CfgEleObj *curr_ele_obj;
  TSCfgEle *ele_copy = 0; /* lv: just to make gcc happy */
  int count          = 0; // start counting from 0
  TSMgmtError ret;
  int tot_ele;

  ink_assert(ctx);
  if (!ctx)
    return TS_ERR_PARAMS;

  tot_ele = CfgContextGetCount(ctx); // inefficient!
  if (index < 0 || index >= tot_ele)
    return TS_ERR_PARAMS;

  // moving the last ele down does nothing
  if (index == (tot_ele - 1))
    return TS_ERR_OKAY;

  // retrieve the ELe and make a copy of it
  curr_ele_obj = ctx->first(); // get head of ele
  while (curr_ele_obj) {
    if (curr_ele_obj->getRuleType() == TS_TYPE_COMMENT) { /* a comment ele */
      curr_ele_obj = ctx->next(curr_ele_obj);
      continue;
    } else {
      if (count == index) {
        // make a copy of ele
        ele_copy = curr_ele_obj->getCfgEleCopy();

        // remove the ele
        ctx->removeEle(curr_ele_obj);
        break;
      }
      curr_ele_obj = ctx->next(curr_ele_obj);
      count++;
    }
  }
  // reached end of CfgContext before hit index
  if (count != index)
    return TS_ERR_FAIL;

  // reinsert  the ele at index+1
  ret = CfgContextInsertEleAt(ctx, ele_copy, index + 1);

  return ret;
}

/*--------------------------------------------------------------
 * CfgContextAppendEle
 *--------------------------------------------------------------
 * Appends the ele to the end of the CfgContext. First must
 * wrap the ele in an CfgEleObj class before can append to CfgContext
 */
TSMgmtError
CfgContextAppendEle(CfgContext *ctx, TSCfgEle *ele)
{
  CfgEleObj *ele_obj;

  ele_obj = create_ele_obj_from_ele(ele);
  ctx->addEle(ele_obj);

  return TS_ERR_OKAY;
}

/*--------------------------------------------------------------
 * CfgContextInsertEleAt
 *--------------------------------------------------------------
 * Inserts the ele into specified position of the CfgContext.  First must
 * wrap the ele in an CfgEleObj class before can insert.
 * If there are comments, before the index specified, the
 * ele will be inserted after the comments.
 * Note: In the special case of inserting in the first position of the list;
 * if there are comments before the current non-comment CfgEleObj,
 * then can just push the new Ele onto the CfgContext. If there are
 * comments before the first non-comment CfgEleObj, then insert the
 * new EleObj right after the last comment
 */
TSMgmtError
CfgContextInsertEleAt(CfgContext *ctx, TSCfgEle *ele, int index)
{
  CfgEleObj *ele_obj, *curr_ele_obj, *last_comment = NULL;
  int count        = 0;
  TSMgmtError err  = TS_ERR_OKAY;
  bool has_comment = false;

  // need to convert the ele into appropriate Ele object type
  ele_obj = create_ele_obj_from_ele(ele);

  // iterate through the ctx, keep count of all NON-Comment Ele Objects
  // when count == index, then insert the new ele object there
  curr_ele_obj = ctx->first(); // get head of ele
  while (curr_ele_obj) {
    if (curr_ele_obj->getRuleType() == TS_TYPE_COMMENT) { /* a comment ele */
      last_comment = curr_ele_obj;
      curr_ele_obj = ctx->next(curr_ele_obj);
      has_comment  = true;
      continue;
    } else {
      // special case if inserting the ele at head of CfgContext
      if (index == 0) {
        if (has_comment) {
          // insert the ele after the last comment
          err = ctx->insertEle(ele_obj, last_comment);
          return err;
        } else { // has no comments preceding first ele object
          err = ctx->pushEle(ele_obj);
          return err;
        }
      }

      if (count == index - 1) { // insert the ele after this one
        err = ctx->insertEle(ele_obj, curr_ele_obj);
        return err; // DONE!
      }
      curr_ele_obj = ctx->next(curr_ele_obj);
      count++;
    }
  }

  return TS_ERR_FAIL; // invalid index
}

/*--------------------------------------------------------------
 * CfgContextRemoveEleAt
 *--------------------------------------------------------------
 * Removes the non-comment Ele at the specified index. start couting
 * non-comment ele's from 0, eg. if index = 3, that means remove the
 * fourth non-comment ELe in the CfgContext
 */
TSMgmtError
CfgContextRemoveEleAt(CfgContext *ctx, int index)
{
  CfgEleObj *curr_ele_obj;
  int count = 0;

  // iterate through the ctx, keep count of all NON-Comment Ele Objects
  // when count == index, then insert the new ele object there
  curr_ele_obj = ctx->first(); // get head of ele
  while (curr_ele_obj) {
    if (curr_ele_obj->getRuleType() == TS_TYPE_COMMENT) { /* a comment ele */
      curr_ele_obj = ctx->next(curr_ele_obj);
      continue;
    } else {
      if (count == index) { // reached the Ele
        ctx->removeEle(curr_ele_obj);
        return TS_ERR_OKAY; // DONE!
      }
      curr_ele_obj = ctx->next(curr_ele_obj);
      count++;
    }
  }

  return TS_ERR_FAIL; // invalid index
}

/*--------------------------------------------------------------
 * CfgContextRemoveAll
 *--------------------------------------------------------------
 * Removes all the Ele rules from Cfg Context leaving all the
 * comments behind
 */
TSMgmtError
CfgContextRemoveAll(CfgContext *ctx)
{
  CfgEleObj *curr_ele_obj, *ele_obj_ptr;

  curr_ele_obj = ctx->first();
  while (curr_ele_obj) {
    if (curr_ele_obj->getRuleType() == TS_TYPE_COMMENT) { // skip comments
      curr_ele_obj = ctx->next(curr_ele_obj);
      continue;
    } else {
      ele_obj_ptr = ctx->next(curr_ele_obj);
      ctx->removeEle(curr_ele_obj);
    }
    curr_ele_obj = ele_obj_ptr;
  }

  return TS_ERR_OKAY;
}
