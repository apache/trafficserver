/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
Regression test code for TS CPP API header HttpMsgComp.h.  The code assumes there will only be one active transaction at a time.
*/

#include <fstream>
#include <cstdlib>
#include <string_view>
#include <string>
#include <cstring>
#include <memory>

#include <ts/ts.h>
#include <tscpp/util/TextView.h>

#include <tscpp/api/HttpMsgComp.h>

// TSReleaseAssert() doesn't seem to produce any logging output for a debug build, so do both kinds of assert.
//
#if defined(__OPTIMIZE__)

#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSReleaseAssert(EXPR);  \
  }

#else

#define ALWAYS_ASSERT(EXPR) \
  {                         \
    TSAssert(EXPR);         \
  }

#endif

namespace
{
#define PINAME "msg_comp"
char PIName[] = PINAME;

// NOTE:  It's important to flush this after writing so that a gold test using this plugin can examine the log before TS
// terminates.
//
std::fstream logFile;

using atscppapi::txnRemapFromUrlStringGet;
using atscppapi::txnRemapToUrlStringGet;
using atscppapi::txnEffectiveUrlStringGet;
using atscppapi::MsgBase;
using atscppapi::MimeField;
using atscppapi::ReqMsg;
using atscppapi::RespMsg;
using atscppapi::TxnClientReq;
using atscppapi::TxnClientResp;
using atscppapi::TxnServerReq;
using atscppapi::TxnServerResp;

bool
eq_values_list(ts::TextView list1, ts::TextView list2)
{
  const char space_tab[] = " \t";
  ts::TextView next1;
  ts::TextView next2;

  do {
    next1 = list1.take_prefix_at(',');
    next2 = list2.take_prefix_at(',');
    next1.trim(space_tab);
    if (next1 != next2.trim(space_tab)) {
      return false;
    }
  } while (next1);

  return !next2;
}

void
dumpMimeField(const MimeField &fld)
{
  logFile << fld.nameGet() << ": ";

  int nVals = fld.valuesCount();
  std::string all;

  if (nVals > 0) {
    all += fld.valGet(0);

    for (int i = 1; i < nVals; ++i) {
      all += ", ";
      all += fld.valGet(i);
    }
  }
  ALWAYS_ASSERT(eq_values_list(fld.valuesGet(), all));
  logFile << all << std::endl;
}

bool
sameMimeField(const MimeField &f1, const MimeField &f2)
{
  if (f1.msg() != f2.msg()) {
    return false;
  }

#if 0
  if (f1.loc() != f2.loc()) {
#else
  // Presumably as some sort of homage to Satan, trafficserver seems to make duplicate copies of the same MIME header
  // within a message.
  //
  if (f2.valuesGet() != f2.valuesGet()) {
#endif
  return false;
}

return true;
} // namespace

void
dumpMsg(MsgBase &msg)
{
  ALWAYS_ASSERT(!(msg != msg));

  logFile << "version=" << msg.httpVersionGet().major() << '.' << msg.httpVersionGet().minor() << std::endl;
  logFile << "hdrLength=" << msg.hdrLength() << std::endl;

  int nFlds = msg.mimeFieldsCount();

  if (nFlds) {
    MimeField iterFld(msg, 0);

    for (int i = 0; i < nFlds; ++i) {
      MimeField currFld(msg, i);

      ALWAYS_ASSERT(sameMimeField(currFld, iterFld));
      iterFld = iterFld.next();

      MimeField fFld(msg, currFld.nameGet());
      ALWAYS_ASSERT(fFld.valid());

      dumpMimeField(currFld);
    }
    ALWAYS_ASSERT(!iterFld.valid());
  }
}

void
dumpReqMsg(ReqMsg &msg, std::string_view msgName)
{
  ALWAYS_ASSERT(msg.type() == MsgBase::Type::REQUEST);

  logFile << std::endl << msgName << ':' << std::endl;
  logFile << "method=" << msg.methodGet() << std::endl;

  int urlLength = msg.absoluteUrl(nullptr, 0);
  ALWAYS_ASSERT(urlLength > 0);
  std::unique_ptr<char[]> url(new char[urlLength]);
  ALWAYS_ASSERT(msg.absoluteUrl(url.get(), urlLength) == urlLength);
  std::unique_ptr<char[]> url2(new char[10]);
  ALWAYS_ASSERT(msg.absoluteUrl(url2.get(), 10) == urlLength);
  if (urlLength <= 10) {
    ALWAYS_ASSERT(std::memcmp(url.get(), url2.get(), urlLength) == 0);
  }
  url2.reset(new char[1000]);
  ALWAYS_ASSERT(msg.absoluteUrl(url2.get(), 1000) == urlLength);
  if (urlLength <= 1000) {
    ALWAYS_ASSERT(std::memcmp(url.get(), url2.get(), urlLength) == 0);
  }
  logFile << "absUrl=" << std::string_view(url.get(), urlLength) << std::endl;

  dumpMsg(msg);
}

void
dumpRespMsg(RespMsg &msg, std::string_view msgName)
{
  ALWAYS_ASSERT(msg.type() == MsgBase::Type::RESPONSE);

  logFile << std::endl << msgName << ':' << std::endl;
  logFile << "status=" << static_cast<int>(msg.statusGet()) << std::endl;
  logFile << "reason=" << msg.reasonGet() << std::endl;

  dumpMsg(msg);
}

void
doCrap(MsgBase &msg, bool add)
{
  MimeField f(msg, "x-crap");

  ALWAYS_ASSERT(f.valuesCount() == 3);
  ALWAYS_ASSERT(f.valGet(0) == "one");
  ALWAYS_ASSERT(f.valGet(1) == "two");
  ALWAYS_ASSERT(f.valGet(2) == "three");
  ALWAYS_ASSERT(f.valuesGet() == "one, two, three");

  MimeField fd(f.nextDup());

  ALWAYS_ASSERT(fd.valuesCount() == 1);
  ALWAYS_ASSERT(fd.valGet(0) == "four");
  ALWAYS_ASSERT(fd.valuesGet() == "four");

  if (add) {
    fd.valInsert(0, "Three-And-A-Half");

    ALWAYS_ASSERT(fd.valuesCount() == 2);

    fd.valAppend("five");
    fd.valSet(1, "cuatro");

    ALWAYS_ASSERT(fd.valuesCount() == 3);
    ALWAYS_ASSERT(fd.valGet(0) == "Three-And-A-Half");
    ALWAYS_ASSERT(fd.valGet(1) == "cuatro");
    ALWAYS_ASSERT(fd.valGet(2) == "five");
    fd.valuesSet();
    ALWAYS_ASSERT(fd.valuesGet() == "");
    fd.valuesSet("\talpha\t, beta  , gamma");
    ALWAYS_ASSERT(fd.valuesCount() == 3);
    ALWAYS_ASSERT(fd.valuesGet() == "\talpha\t, beta  , gamma");

    MimeField ld = MimeField::lastDup(fd.msg(), "X-Crap");
    ALWAYS_ASSERT(sameMimeField(fd, ld));
  }
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  logFile << "Global: event=" << TSHttpEventNameLookup(event) << std::endl;

  TSDebug(PIName, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    auto txn = static_cast<TSHttpTxn>(eventData);

    logFile << std::endl;
    logFile << "Remap From URL: " << txnRemapFromUrlStringGet(txn).asStringView() << std::endl;
    logFile << "Remap To   URL: " << txnRemapToUrlStringGet(txn).asStringView() << std::endl;
    logFile << "Effective  URL: " << txnEffectiveUrlStringGet(txn).asStringView() << std::endl;

    // This block ensures destruction of objects before the Transaction reenable call.
    {
      TxnClientReq clientReq(txn);
      sameMimeField(MimeField(clientReq, "Host"), MimeField::lastDup(clientReq, "Host"));
      doCrap(clientReq, false);
      dumpReqMsg(clientReq, "Client Request");

      TxnClientResp clientResp(txn);
      dumpRespMsg(clientResp, "Client Response");

      TxnServerReq serverReq(txn);
      doCrap(serverReq, true);
      dumpReqMsg(serverReq, "Server Request");

      TxnServerResp serverResp(txn);
      dumpRespMsg(serverResp, "Server Response");
    }

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  } break;

  default: {
    ALWAYS_ASSERT(false)
  } break;

  } // end switch

  return 0;
}

TSCont gCont;

} // end anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed");

    return;
  }

  const char *fileSpec = std::getenv("OUTPUT_FILE");

  if (nullptr == fileSpec) {
    TSError(PINAME ": Environment variable OUTPUT_FILE not found.");

    return;
  }

  // Disable output buffering for logFile, so that explicit flushing is not necessary.
  logFile.rdbuf()->pubsetbuf(nullptr, 0);

  logFile.open(fileSpec, std::ios::out);
  if (!logFile.is_open()) {
    TSError(PINAME ": could not open log file \"%s\"", fileSpec);

    return;
  }

  gCont = TSContCreate(globalContFunc, nullptr);

  // Setup the global hook
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, gCont);
}
