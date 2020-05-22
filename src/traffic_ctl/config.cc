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

#include "traffic_ctl.h"
#include <ctime>
#include "records/I_RecDefs.h"
#include "records/P_RecUtils.h"
#include "ts/apidefs.h"
#include "HTTP.h"
#include "HttpConnectionCount.h"
#include "shared/overridable_txn_vars.h"
#include <tscore/BufferWriter.h>

struct RecordDescriptionPolicy {
  using entry_type = TSConfigRecordDescription *;

  static void
  free(entry_type e)
  {
    TSConfigRecordDescriptionDestroy(e);
  }

  static entry_type
  cast(void *ptr)
  {
    return (entry_type)ptr;
  }
};

struct CtrlMgmtRecordDescriptionList : CtrlMgmtList<RecordDescriptionPolicy> {
  TSMgmtError
  match(const char *regex)
  {
    return TSConfigRecordDescribeMatchMlt(regex, 0u /* flags */, this->list);
  }
};

// Record data type names, indexed by TSRecordT.
static const char *
rec_typeof(int rec_type)
{
  switch (rec_type) {
  case TS_REC_INT:
    return "INT";
  case TS_REC_COUNTER:
    return "COUNTER";
  case TS_REC_FLOAT:
    return "FLOAT";
  case TS_REC_STRING:
    return "STRING";
  case TS_REC_UNDEFINED: /* fallthrough */
  default:
    return "UNDEFINED";
  }
}

// Record type name, indexed by RecT.
static const char *
rec_classof(int rec_class)
{
  switch (rec_class) {
  case RECT_CONFIG:
    return "standard config";
  case RECT_LOCAL:
    return "local config";
  case RECT_PROCESS:
    return "process metric";
  case RECT_NODE:
    return "node metric";
  case RECT_PLUGIN:
    return "plugin metric";
  default:
    return "undefined";
  }
}

// Record access control, indexed by RecAccessT.
static const char *
rec_accessof(int rec_access)
{
  switch (rec_access) {
  case RECA_NO_ACCESS:
    return "no access";
  case RECA_READ_ONLY:
    return "read only";
  case RECA_NULL: /* fallthrough */
  default:
    return "default";
  }
}

// Record access control, indexed by RecUpdateT.
static const char *
rec_updateof(int rec_updatetype)
{
  switch (rec_updatetype) {
  case RECU_DYNAMIC:
    return "dynamic, no restart";
  case RECU_RESTART_TS:
    return "static, restart traffic_server";
  case RECU_RESTART_TM:
    return "static, restart traffic_manager";
  case RECU_NULL: /* fallthrough */
  default:
    return "none";
  }
}

// Record check type, indexed by RecCheckT.
static const char *
rec_checkof(int rec_checktype)
{
  switch (rec_checktype) {
  case RECC_STR:
    return "string matching a regular expression";
  case RECC_INT:
    return "integer with a specified range";
  case RECC_IP:
    return "IP address";
  case RECC_NULL: /* fallthrough */
  default:
    return "none";
  }
}

static const char *
rec_sourceof(int rec_source)
{
  switch (rec_source) {
  case REC_SOURCE_DEFAULT:
    return "built in default";
  case REC_SOURCE_EXPLICIT:
    return "administratively set";
  case REC_SOURCE_PLUGIN:
    return "plugin default";
  case REC_SOURCE_ENV:
    return "environment";
  default:
    return "unknown";
  }
}

static const char *
rec_labelof(int rec_class)
{
  switch (rec_class) {
  case RECT_CONFIG:
    return "CONFIG";
  case RECT_LOCAL:
    return "LOCAL";
  default:
    return nullptr;
  }
}

static const char *
rec_datatypeof(TSRecordDataType dt)
{
  switch (dt) {
  case TS_RECORDDATATYPE_INT:
    return "int";
  case TS_RECORDDATATYPE_NULL:
    return "null";
  case TS_RECORDDATATYPE_FLOAT:
    return "float";
  case TS_RECORDDATATYPE_STRING:
    return "string";
  case TS_RECORDDATATYPE_COUNTER:
    return "counter";
  case TS_RECORDDATATYPE_STAT_CONST:
    return "constant stat";
  case TS_RECORDDATATYPE_STAT_FX:
    return "stat fx";
  case TS_RECORDDATATYPE_MAX:
    return "*";
  }
  return "?";
}

static std::string
timestr(time_t tm)
{
  char buf[32];
  return std::string(ctime_r(&tm, buf));
}

static void
format_record(const CtrlMgmtRecord &record, bool recfmt)
{
  CtrlMgmtRecordValue value(record);

  if (recfmt) {
    std::cout << rec_labelof(record.rclass()) << ' ' << record.name() << ' ' << rec_typeof(record.type()) << ' ' << value.c_str()
              << std::endl;
  } else {
    std::cout << record.name() << ": " << value.c_str() << std::endl;
  }
}

void
CtrlEngine::config_get()
{
  for (const auto &it : arguments.get("get")) {
    CtrlMgmtRecord record;
    TSMgmtError error;

    error = record.fetch(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }

    if (REC_TYPE_IS_CONFIG(record.rclass())) {
      format_record(record, arguments.get("records"));
    }
  }
}

void
CtrlEngine::config_describe()
{
  for (const auto &it : arguments.get("describe")) {
    TSConfigRecordDescription desc;
    TSMgmtError error;

    ink_zero(desc);
    error = TSConfigRecordDescribe(it.c_str(), 0 /* flags */, &desc);
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to describe %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }

    auto ov_iter       = ts::Overridable_Txn_Vars.find(it);
    bool overridable_p = (ov_iter != ts::Overridable_Txn_Vars.end());

    std::string text;
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Name", desc.rec_name);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Current Value ", CtrlMgmtRecordValue(desc.rec_type, desc.rec_value).c_str());
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Default Value ", CtrlMgmtRecordValue(desc.rec_type, desc.rec_default).c_str());
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Record Type ", rec_classof(desc.rec_class));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Data Type ", rec_typeof(desc.rec_type));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Access Control ", rec_accessof(desc.rec_access));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Type ", rec_updateof(desc.rec_updatetype));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Status ", desc.rec_update);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Source ", rec_sourceof(desc.rec_source));
    std::cout << ts::bwprint(text, "{:16s}: {} {}\n", "Overridable", overridable_p ? "yes" : "no",
                             overridable_p ? rec_datatypeof(std::get<1>(ov_iter->second)) : "");

    if (strlen(desc.rec_checkexpr)) {
      std::cout << ts::bwprint(text, "{:16s}: {}\n", "Syntax Check ", rec_checkof(desc.rec_checktype), desc.rec_checkexpr);
    } else {
      std::cout << ts::bwprint(text, "{:16s}: {}\n", "Syntax Check ", rec_checkof(desc.rec_checktype));
    }

    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Version ", desc.rec_version);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Order ", desc.rec_order);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Raw Stat Block ", desc.rec_rsb);

    TSConfigRecordDescriptionFree(&desc);
  }
}

void
CtrlEngine::config_set()
{
  TSMgmtError error;
  TSActionNeedT action;
  auto set_data        = arguments.get("set");
  const char *rec_name = set_data[0].c_str();
  const char *rec_val  = set_data[1].c_str();
  error                = TSRecordSet(rec_name, rec_val, &action);
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to set %s", rec_name);
    status_code = CTRL_EX_ERROR;
    return;
  }

  switch (action) {
  case TS_ACTION_SHUTDOWN:
    std::cout << "set " << rec_name << ", full shutdown required" << std::endl;
    break;
  case TS_ACTION_RESTART:
    std::cout << "set " << rec_name << ", restart required" << std::endl;
    break;
  case TS_ACTION_RECONFIGURE:
    std::cout << "set " << rec_name << ", please wait 10 seconds for traffic server to sync configuration, restart is not required"
              << std::endl;
    break;
  case TS_ACTION_DYNAMIC:
  default:
    printf("set %s\n", rec_name);
    break;
  }
}

void
CtrlEngine::config_match()
{
  for (const auto &it : arguments.get("match")) {
    CtrlMgmtRecordList reclist;
    TSMgmtError error;

    // XXX filter the results to only match configuration records.

    error = reclist.match(it.c_str());
    if (error != TS_ERR_OKAY) {
      CtrlMgmtError(error, "failed to fetch %s", it.c_str());
      status_code = CTRL_EX_ERROR;
      return;
    }

    while (!reclist.empty()) {
      CtrlMgmtRecord record(reclist.next());
      if (REC_TYPE_IS_CONFIG(record.rclass())) {
        format_record(record, arguments.get("records"));
      }
    }
  }
}

void
CtrlEngine::config_reload()
{
  TSMgmtError error = TSReconfigure();
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "configuration reload request failed");
    status_code = CTRL_EX_ERROR;
    return;
  }
}

void
CtrlEngine::config_status()
{
  CtrlMgmtRecord version;
  CtrlMgmtRecord configtime;
  CtrlMgmtRecord starttime;
  CtrlMgmtRecord reconfig;
  CtrlMgmtRecord proxy;
  CtrlMgmtRecord manager;

  CTRL_MGMT_CHECK(version.fetch("proxy.process.version.server.long"));
  CTRL_MGMT_CHECK(starttime.fetch("proxy.node.restarts.proxy.start_time"));
  CTRL_MGMT_CHECK(configtime.fetch("proxy.node.config.reconfigure_time"));
  CTRL_MGMT_CHECK(reconfig.fetch("proxy.node.config.reconfigure_required"));
  CTRL_MGMT_CHECK(proxy.fetch("proxy.node.config.restart_required.proxy"));
  CTRL_MGMT_CHECK(manager.fetch("proxy.node.config.restart_required.manager"));

  std::cout << CtrlMgmtRecordValue(version).c_str() << std::endl;
  std::cout << "Started at " << timestr((time_t)starttime.as_int()).c_str();
  std::cout << "Last reconfiguration at " << timestr((time_t)configtime.as_int()).c_str();
  std::cout << (reconfig.as_int() ? "Reconfiguration required" : "Configuration is current") << std::endl;

  if (proxy.as_int()) {
    std::cout << "traffic_server requires restarting" << std::endl;
  }
  if (manager.as_int()) {
    std::cout << "traffic_manager requires restarting\n" << std::endl;
  }
}

void
CtrlEngine::config_defaults()
{
  TSMgmtError error;
  CtrlMgmtRecordDescriptionList descriptions;

  error = descriptions.match(".*");
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch record metadata");
    status_code = CTRL_EX_ERROR;
    return;
  }

  while (!descriptions.empty()) {
    TSConfigRecordDescription *desc = descriptions.next();
    CtrlMgmtRecordValue deflt(desc->rec_type, desc->rec_default);

    if (arguments.get("records")) {
      std::cout << rec_labelof(desc->rec_class) << ' ' << desc->rec_name << ' ' << rec_typeof(desc->rec_type) << ' '
                << deflt.c_str() << std::endl;
    } else {
      std::cout << desc->rec_name << ": " << deflt.c_str() << std::endl;
    }
    TSConfigRecordDescriptionDestroy(desc);
  }
}

void
CtrlEngine::config_diff()
{
  TSMgmtError error;
  CtrlMgmtRecordDescriptionList descriptions;

  error = descriptions.match(".*");
  if (error != TS_ERR_OKAY) {
    CtrlMgmtError(error, "failed to fetch record metadata");
    status_code = CTRL_EX_ERROR;
    return;
  }

  while (!descriptions.empty()) {
    TSConfigRecordDescription *desc;
    bool changed = false;

    desc = descriptions.next();

    switch (desc->rec_type) {
    case TS_REC_INT:
      changed = (desc->rec_value.int_val != desc->rec_default.int_val);
      break;
    case TS_REC_COUNTER:
      changed = (desc->rec_value.counter_val != desc->rec_default.counter_val);
      break;
    case TS_REC_FLOAT:
      changed = (desc->rec_value.float_val != desc->rec_default.float_val);
      break;
    case TS_REC_STRING:
      changed = (strcmp(desc->rec_value.string_val, desc->rec_default.string_val) != 0);
      break;
    default:
      break;
    }

    if (changed) {
      CtrlMgmtRecordValue current(desc->rec_type, desc->rec_value);
      CtrlMgmtRecordValue deflt(desc->rec_type, desc->rec_default);

      if (arguments.get("records")) {
        std::cout << rec_labelof(desc->rec_class) << ' ' << desc->rec_name << ' ' << rec_typeof(desc->rec_type) << ' '
                  << current.c_str() << " # default: " << deflt.c_str() << std::endl;
      } else {
        std::cout << desc->rec_name << " has changed" << std::endl;
        std::cout << "\tCurrent Value: " << current.c_str() << std::endl;
        std::cout << "\tDefault Value: " << deflt.c_str() << std::endl;
      }
    }

    TSConfigRecordDescriptionDestroy(desc);
  }
}
