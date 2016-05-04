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

#include <stdlib.h>
#include <sys/time.h>
#include <ts/ts.h>

#define PLUGIN_NAME "limit_rate"

static int64_t lr = 0;

typedef struct {
  TSIOBufferReader iobrp;
  TSIOBuffer iobp;
  TSVIO viop;
  double t;
} LRData;

int
limit_rate_transform(TSCont contp, TSEvent event, void *edata)
{
  LRData *lrdp;
  TSIOBufferReader iobrp;
  TSVIO viop;
  double t;
  int64_t avail, limit;
  struct timeval tv;

  lrdp = TSContDataGet(contp);
  if (TSVConnClosedGet(contp)) {
    if (lrdp != NULL) {
      TSIOBufferReaderFree(lrdp->iobrp);
      TSIOBufferDestroy(lrdp->iobp);
      TSfree(lrdp);
    }
    TSContDestroy(contp);
    return TS_SUCCESS;
  }
  viop = TSVConnWriteVIOGet(contp);
  if (TSVIOBufferGet(viop) == NULL) {
    if (lrdp != NULL) {
      TSVIONBytesSet(lrdp->viop, TSVIONDoneGet(viop));
      TSVIOReenable(lrdp->viop);
    }
    return TS_SUCCESS;
  }
  gettimeofday(&tv, NULL);
  t = tv.tv_sec + tv.tv_usec * 0.000001;
  if (lrdp == NULL) {
    if ((lrdp = TSmalloc(sizeof(LRData))) == NULL) {
      TSContCall(TSVIOContGet(viop), TS_EVENT_ERROR, viop);
      return TS_SUCCESS;
    }
    lrdp->t     = t - 0.1;
    lrdp->iobp  = TSIOBufferCreate();
    lrdp->iobrp = TSIOBufferReaderAlloc(lrdp->iobp);
    lrdp->viop  = TSVConnWrite(TSTransformOutputVConnGet(contp), contp, lrdp->iobrp, INT64_MAX);
    TSContDataSet(contp, lrdp);
  }
  switch (event) {
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_VCONN_WRITE_READY:
    limit = (t - lrdp->t) * lr;
    if (TSVIONDoneGet(viop) >= limit) {
      TSVIOReenable(viop);
      break;
    }
    iobrp = TSVIOReaderGet(viop);
    if ((avail = TSIOBufferReaderAvail(iobrp)) > 0) {
      if (TSVIONDoneGet(viop) + avail > limit) {
        avail = limit - TSVIONDoneGet(viop);
      }
      TSIOBufferCopy(TSVIOBufferGet(lrdp->viop), iobrp, avail, 0);
      TSIOBufferReaderConsume(iobrp, avail);
      TSVIONDoneSet(viop, TSVIONDoneGet(viop) + avail);
    }
    if (TSVIONTodoGet(viop) > 0) {
      if (avail > 0) {
        TSVIOReenable(lrdp->viop);
        TSContCall(TSVIOContGet(viop), TS_EVENT_VCONN_WRITE_READY, viop);
      }
    } else {
      TSContCall(TSVIOContGet(viop), TS_EVENT_VCONN_WRITE_COMPLETE, viop);
    }
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;
  default:
    TSDebug(PLUGIN_NAME, "event %d", event);
    TSContCall(TSVIOContGet(viop), TS_EVENT_ERROR, viop);
  }
  return TS_SUCCESS;
}

int
txn_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, TSTransformCreate(limit_rate_transform, txnp));
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSDebug(PLUGIN_NAME, "%d", event);
  }
  return TS_SUCCESS;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp;
  TSPluginRegistrationInfo info;
  char *ptr;
  long l;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "The Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
#if (TS_VERSION_NUMBER < 6000000)
  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
#else
  if (TSPluginRegister(&info) != TS_SUCCESS) {
#endif
    return TSError("TSPluginRegister failed");
  }
  if (argc > 1) {
    TSDebug(PLUGIN_NAME, "%s", argv[1]);
    if ((l = strtol(argv[1], &ptr, 10)) > 0) {
      switch (*ptr) {
      case 'K':
        lr = l * 1024;
        break;
      case 'M':
        lr = l * 1048576;
        break;
      case 'G':
        lr = l * 1073741824;
        break;
      default:
        lr = l;
      }
      if ((contp = TSContCreate(txn_handler, NULL)) == NULL) {
        return TSError("TSContCreate failed");
      }
      TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp);
    }
  }
}
