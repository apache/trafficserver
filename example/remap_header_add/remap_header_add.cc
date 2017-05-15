/** @file

  A simple remap plugin for ATS

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

  @section description
    This is a very simple plugin: it will add headers that are specified on a remap line

    Example usage:
    map /foo http://127.0.0.1/ @plugin=remap_header_add.so @pparam=foo:"x" @pparam=@test:"c" @pparam=a:"b"

 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ts/ts.h"
#include "ts/remap.h"

struct remap_line {
  int argc;
  char **argv; // store the originals

  int nvc;     // the number of name value pairs, should be argc - 2.
  char **name; // at load we will parse out the name and values.
  char **val;
};

#define PLUGIN_NAME "headeradd_remap"
#define EXTERN extern "C"

EXTERN void
ParseArgIntoNv(const char *arg, char **n, char **v)
{
  const char *colon_pos = strchr(arg, ':');

  if (colon_pos == nullptr) {
    *n = nullptr;
    *v = nullptr;
    TSDebug(PLUGIN_NAME, "No name value pair since it was malformed");
    return;
  }

  size_t name_len = colon_pos - arg;
  *n              = static_cast<char *>(TSmalloc(name_len + 1));
  memcpy(*n, arg, colon_pos - arg);
  (*n)[name_len] = '\0';

  size_t val_len = strlen(colon_pos + 1); // skip past the ':'

  // check to see if the value is quoted.
  if (val_len > 1 && colon_pos[1] == '"' && colon_pos[val_len] == '"') {
    colon_pos++;  // advance past the first quote
    val_len -= 2; // don't include the trailing quote
  }

  *v = static_cast<char *>(TSmalloc(val_len + 1));
  memcpy(*v, colon_pos + 1, val_len);
  (*v)[val_len] = '\0';

  TSDebug(PLUGIN_NAME, "\t name_len=%zu, val_len=%zu, %s=%s", name_len, val_len, *n, *v);
}

TSReturnCode
TSRemapInit(TSRemapInterface *, char *, int)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *, int)
{
  remap_line *rl = nullptr;

  TSDebug(PLUGIN_NAME, "TSRemapNewInstance()");

  if (!argv || !ih) {
    TSError("[%s] Unable to load plugin because missing argv or ih", PLUGIN_NAME);
    return TS_ERROR;
  }

  // print all arguments for this particular remapping

  rl       = (remap_line *)TSmalloc(sizeof(remap_line));
  rl->argc = argc;
  rl->argv = argv;
  rl->nvc  = argc - 2; // the first two are the remap from and to
  if (rl->nvc) {
    rl->name = static_cast<char **>(TSmalloc(sizeof(char *) * rl->nvc));
    rl->val  = static_cast<char **>(TSmalloc(sizeof(char *) * rl->nvc));
  }

  TSDebug(PLUGIN_NAME, "NewInstance:");
  for (int i = 2; i < argc; i++) {
    ParseArgIntoNv(argv[i], &rl->name[i - 2], &rl->val[i - 2]);
  }

  *ih = rl;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(PLUGIN_NAME, "deleting instance %p", ih);

  if (ih) {
    remap_line *rl = static_cast<remap_line *>(ih);
    for (int i = 0; i < rl->nvc; ++i) {
      TSfree(rl->name[i]);
      TSfree(rl->val[i]);
    }

    TSfree(rl->name);
    TSfree(rl->val);
    TSfree(rl);
  }
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txn, TSRemapRequestInfo *rri)
{
  remap_line *rl = static_cast<remap_line *>(ih);

  if (!rl || !rri) {
    TSError("[%s] rl or rri is null", PLUGIN_NAME);
    return TSREMAP_NO_REMAP;
  }

  TSDebug(PLUGIN_NAME, "TSRemapDoRemap:");

  TSMBuffer req_bufp;
  TSMLoc req_loc;
  if (TSHttpTxnClientReqGet(txn, &req_bufp, &req_loc) != TS_SUCCESS) {
    TSError("[%s] Error while retrieving client request header", PLUGIN_NAME);
    return TSREMAP_NO_REMAP;
  }

  for (int i = 0; i < rl->nvc; ++i) {
    TSDebug(PLUGIN_NAME, R"(Attaching header "%s" with value "%s".)", rl->name[i], rl->val[i]);

    TSMLoc field_loc;
    if (TSMimeHdrFieldCreate(req_bufp, req_loc, &field_loc) == TS_SUCCESS) {
      TSMimeHdrFieldNameSet(req_bufp, req_loc, field_loc, rl->name[i], strlen(rl->name[i]));
      TSMimeHdrFieldAppend(req_bufp, req_loc, field_loc);
      TSMimeHdrFieldValueStringInsert(req_bufp, req_loc, field_loc, 0, rl->val[i], strlen(rl->val[i]));
    } else {
      TSError("[%s] Failure on TSMimeHdrFieldCreate", PLUGIN_NAME);
    }
  }

  return TSREMAP_NO_REMAP;
}
