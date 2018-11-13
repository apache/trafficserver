/** @file

  traffic_ctl

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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/Diags.h"
#include "tscore/ink_memory.h"
#include "mgmtapi.h"
#include "tscore/ink_args.h"
#include "tscore/I_Version.h"
#include "tscore/BaseLogFile.h"
#include "tscore/ArgParser.h"

#include <vector>
#include <string>
#include <iostream>

// Exit status codes, following BSD's sysexits(3)
constexpr int CTRL_EX_OK            = 0;
constexpr int CTRL_EX_ERROR         = 2;
constexpr int CTRL_EX_UNIMPLEMENTED = 3;
constexpr int CTRL_EX_USAGE         = EX_USAGE;
constexpr int CTRL_EX_UNAVAILABLE   = 69;

#define CtrlDebug(...) Debug("traffic_ctl", __VA_ARGS__)

void CtrlMgmtError(TSMgmtError err, const char *fmt, ...) TS_PRINTFLIKE(2, 3);

#define CTRL_MGMT_CHECK(expr)                          \
  do {                                                 \
    TSMgmtError e = (expr);                            \
    if (e != TS_ERR_OKAY) {                            \
      CtrlDebug("%s failed with status %d", #expr, e); \
      CtrlMgmtError(e, nullptr);                       \
      status_code = CTRL_EX_ERROR;                     \
      return;                                          \
    }                                                  \
  } while (0)

struct CtrlMgmtRecord {
  explicit CtrlMgmtRecord(TSRecordEle *e) : ele(e) {}
  CtrlMgmtRecord() : ele(TSRecordEleCreate()) {}
  ~CtrlMgmtRecord()
  {
    if (this->ele) {
      TSRecordEleDestroy(this->ele);
    }
  }

  TSMgmtError fetch(const char *);

  const char *name() const;
  TSRecordT type() const;
  int rclass() const;
  int64_t as_int() const;

  // noncopyable
  CtrlMgmtRecord(const CtrlMgmtRecord &) = delete;            // disabled
  CtrlMgmtRecord &operator=(const CtrlMgmtRecord &) = delete; // disabled

private:
  TSRecordEle *ele;

  friend struct CtrlMgmtRecordValue;
};

struct CtrlMgmtRecordValue {
  explicit CtrlMgmtRecordValue(const TSRecordEle *);
  explicit CtrlMgmtRecordValue(const CtrlMgmtRecord &);

  CtrlMgmtRecordValue(TSRecordT, TSRecordValueT);
  const char *c_str() const;

  // noncopyable
  CtrlMgmtRecordValue(const CtrlMgmtRecordValue &) = delete;            // disabled
  CtrlMgmtRecordValue &operator=(const CtrlMgmtRecordValue &) = delete; // disabled

private:
  void init(TSRecordT, TSRecordValueT);

  TSRecordT rec_type;
  union {
    const char *str;
    char nbuf[32];
  } fmt;
};

struct RecordListPolicy {
  typedef TSRecordEle *entry_type;

  static void
  free(entry_type e)
  {
    TSRecordEleDestroy(e);
  }

  static entry_type
  cast(void *ptr)
  {
    return (entry_type)ptr;
  }
};

template <typename T> struct CtrlMgmtList {
  CtrlMgmtList() : list(TSListCreate()) {}
  ~CtrlMgmtList()
  {
    this->clear();
    TSListDestroy(this->list);
  }

  bool
  empty() const
  {
    return TSListIsEmpty(this->list);
  }

  void
  clear() const
  {
    while (!this->empty()) {
      T::free(T::cast(TSListDequeue(this->list)));
    }
  }

  // Return (ownership of) the next list entry.
  typename T::entry_type
  next()
  {
    return T::cast(TSListDequeue(this->list));
  }

  TSList list;

  // noncopyable
  CtrlMgmtList(const CtrlMgmtList &) = delete;            // disabled
  CtrlMgmtList &operator=(const CtrlMgmtList &) = delete; // disabled
};

struct CtrlMgmtRecordList : CtrlMgmtList<RecordListPolicy> {
  TSMgmtError match(const char *);
};

// this is a engine for traffic_ctl containing the ArgParser and all the methods
// it also has a status code which can be set by these methods to return
struct CtrlEngine {
  // the parser for traffic_ctl
  ts::ArgParser parser;
  // parsed arguments
  ts::Arguments arguments;
  // the return status code from functions
  // By default it is set to CTRL_EX_OK so we don't need to set it
  // in each method when they finish successfully.
  int status_code = CTRL_EX_OK;

  // All traffic_ctl methods:
  // umimplemented command
  void CtrlUnimplementedCommand(std::string_view command);

  // alarm methods
  void alarm_list();
  void alarm_clear();
  void alarm_resolve();

  // config methods
  void config_defaults();
  void config_diff();
  void config_status();
  void config_reload();
  void config_match();
  void config_get();
  void config_set();
  void config_describe();

  // host methods
  void status_get();
  void status_down();
  void status_up();

  // metric methods
  void metric_get();
  void metric_match();
  void metric_clear();
  void metric_zero();

  // metric methods
  void plugin_msg();

  // server methods
  void server_restart();
  void server_backtrace();
  void server_status();
  void server_stop();
  void server_start();
  void server_drain();

  // storage methods
  void storage_offline();
};
