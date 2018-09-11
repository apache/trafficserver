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

#include <vector>
#include <string>

struct subcommand {
  int (*handler)(unsigned, const char **);
  const char *name;
  const char *help;
};

extern AppVersionInfo CtrlVersionInfo;

#define CtrlDebug(...) Debug("traffic_ctl", __VA_ARGS__)

void CtrlMgmtError(TSMgmtError err, const char *fmt, ...) TS_PRINTFLIKE(2, 3);
int CtrlSubcommandUsage(const char *name, const subcommand *cmds, unsigned ncmds, const ArgumentDescription *desc, unsigned ndesc);
int CtrlCommandUsage(const char *msg, const ArgumentDescription *desc = nullptr, unsigned ndesc = 0);

bool CtrlProcessArguments(int argc, const char **argv, const ArgumentDescription *desc, unsigned ndesc);
int CtrlUnimplementedCommand(unsigned argc, const char **argv);

int CtrlGenericSubcommand(const char *, const subcommand *cmds, unsigned ncmds, unsigned argc, const char **argv);

#define CTRL_MGMT_CHECK(expr)                          \
  do {                                                 \
    TSMgmtError e = (expr);                            \
    if (e != TS_ERR_OKAY) {                            \
      CtrlDebug("%s failed with status %d", #expr, e); \
      CtrlMgmtError(e, nullptr);                       \
      return CTRL_EX_ERROR;                            \
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

struct CtrlCommandLine {
  CtrlCommandLine() { this->args.push_back(nullptr); }
  void
  init(unsigned argc, const char **argv)
  {
    this->args.clear();
    for (unsigned i = 0; i < argc; ++i) {
      this->args.push_back(argv[i]);
    }

    // Always nullptr-terminate to keep ink_args happy. Note that we adjust arg() accordingly.
    this->args.push_back(nullptr);
  }

  unsigned
  argc()
  {
    return args.size() - 1;
  }

  const char **
  argv()
  {
    return &args[0];
  }

private:
  std::vector<const char *> args;
};

int subcommand_alarm(unsigned argc, const char **argv);
int subcommand_config(unsigned argc, const char **argv);
int subcommand_metric(unsigned argc, const char **argv);
int subcommand_server(unsigned argc, const char **argv);
int subcommand_storage(unsigned argc, const char **argv);
int subcommand_plugin(unsigned argc, const char **argv);
int subcommand_host(unsigned argc, const char **argv);

// Exit status codes, following BSD's sysexits(3)
#define CTRL_EX_OK 0
#define CTRL_EX_ERROR 2
#define CTRL_EX_UNIMPLEMENTED 3
#define CTRL_EX_USAGE EX_USAGE
#define CTRL_EX_UNAVAILABLE 69
