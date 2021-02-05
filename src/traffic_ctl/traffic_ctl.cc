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

#include "records/I_RecProcess.h"
#include "RecordsConfig.h"
#include "tscore/I_Layout.h"
#include "tscore/runroot.h"

const char *
CtrlMgmtRecord::name() const
{
  return this->ele->rec_name;
}

TSRecordT
CtrlMgmtRecord::type() const
{
  return this->ele->rec_type;
}

int
CtrlMgmtRecord::rclass() const
{
  return this->ele->rec_class;
}

int64_t
CtrlMgmtRecord::as_int() const
{
  switch (this->ele->rec_type) {
  case TS_REC_INT:
    return this->ele->valueT.int_val;
  case TS_REC_COUNTER:
    return this->ele->valueT.counter_val;
  default:
    return 0;
  }
}

TSMgmtError
CtrlMgmtRecord::fetch(const char *name)
{
  return TSRecordGet(name, this->ele);
}

TSMgmtError
CtrlMgmtRecordList::match(const char *name)
{
  return TSRecordGetMatchMlt(name, this->list);
}

CtrlMgmtRecordValue::CtrlMgmtRecordValue(const CtrlMgmtRecord &rec)
{
  this->init(rec.ele->rec_type, rec.ele->valueT);
}

CtrlMgmtRecordValue::CtrlMgmtRecordValue(const TSRecordEle *ele)
{
  this->init(ele->rec_type, ele->valueT);
}

CtrlMgmtRecordValue::CtrlMgmtRecordValue(TSRecordT _t, TSRecordValueT _v)
{
  this->init(_t, _v);
}

void
CtrlMgmtRecordValue::init(TSRecordT _t, TSRecordValueT _v)
{
  this->rec_type = _t;
  switch (this->rec_type) {
  case TS_REC_INT:
    snprintf(this->fmt.nbuf, sizeof(this->fmt.nbuf), "%" PRId64, _v.int_val);
    break;
  case TS_REC_COUNTER:
    snprintf(this->fmt.nbuf, sizeof(this->fmt.nbuf), "%" PRId64, _v.counter_val);
    break;
  case TS_REC_FLOAT:
    snprintf(this->fmt.nbuf, sizeof(this->fmt.nbuf), "%f", _v.float_val);
    break;
  case TS_REC_STRING:
    if (strcmp(_v.string_val, "") == 0) {
      this->fmt.str = "\"\"";
    } else {
      this->fmt.str = _v.string_val;
    }
    break;
  default:
    rec_type      = TS_REC_STRING;
    this->fmt.str = "(invalid)";
  }
}

const char *
CtrlMgmtRecordValue::c_str() const
{
  switch (this->rec_type) {
  case TS_REC_STRING:
    return this->fmt.str;
  default:
    return this->fmt.nbuf;
  }
}

void
CtrlMgmtError(TSMgmtError err, const char *fmt, ...)
{
  ats_scoped_str msg(TSGetErrorMessage(err));

  if (fmt) {
    va_list ap;

    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, ": %s\n", (const char *)msg);
  } else {
    fprintf(stderr, "%s: %s\n", program_name, (const char *)msg);
  }
}

void
CtrlEngine::CtrlUnimplementedCommand(std::string_view command)
{
  fprintf(stderr, "'%s' command is not implemented\n", command.data());
  status_code = CTRL_EX_UNIMPLEMENTED;
}

int
main(int argc, const char **argv)
{
  CtrlEngine engine;

  engine.parser.add_global_usage("traffic_ctl [OPTIONS] CMD [ARGS ...]");
  engine.parser.require_commands();

  engine.parser.add_option("--debug", "", "Enable debugging output")
    .add_option("--version", "-V", "Print version string")
    .add_option("--help", "-h", "Print usage information")
    .add_option("--run-root", "", "using TS_RUNROOT as sandbox", "TS_RUNROOT", 1);

  auto &alarm_command   = engine.parser.add_command("alarm", "Manipulate alarms").require_commands();
  auto &config_command  = engine.parser.add_command("config", "Manipulate configuration records").require_commands();
  auto &metric_command  = engine.parser.add_command("metric", "Manipulate performance metrics").require_commands();
  auto &server_command  = engine.parser.add_command("server", "Stop, restart and examine the server").require_commands();
  auto &storage_command = engine.parser.add_command("storage", "Manipulate cache storage").require_commands();
  auto &plugin_command  = engine.parser.add_command("plugin", "Interact with plugins").require_commands();
  auto &host_command    = engine.parser.add_command("host", "Interact with host status").require_commands();

  // alarm commands
  alarm_command.add_command("clear", "Clear all current alarms", [&]() { engine.alarm_clear(); })
    .add_example_usage("traffic_ctl alarm clear");
  alarm_command.add_command("list", "List all current alarms", [&]() { engine.alarm_list(); })
    .add_example_usage("traffic_ctl alarm list");
  alarm_command.add_command("resolve", "Resolve the listed alarms", "", MORE_THAN_ONE_ARG_N, [&]() { engine.alarm_resolve(); })
    .add_example_usage("traffic_ctl alarm resolve ALARM [ALARM ...]");

  // config commands
  config_command.add_command("defaults", "Show default information configuration values", [&]() { engine.config_defaults(); })
    .add_example_usage("traffic_ctl config defaults [OPTIONS]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command
    .add_command("describe", "Show detailed information about configuration values", "", MORE_THAN_ONE_ARG_N,
                 [&]() { engine.config_describe(); })
    .add_example_usage("traffic_ctl config describe RECORD [RECORD ...]");
  config_command.add_command("diff", "Show non-default configuration values", [&]() { engine.config_diff(); })
    .add_example_usage("traffic_ctl config diff [OPTIONS]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command.add_command("get", "Get one or more configuration values", "", MORE_THAN_ONE_ARG_N, [&]() { engine.config_get(); })
    .add_example_usage("traffic_ctl config get [OPTIONS] RECORD [RECORD ...]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command
    .add_command("match", "Get configuration matching a regular expression", "", MORE_THAN_ONE_ARG_N,
                 [&]() { engine.config_match(); })
    .add_example_usage("traffic_ctl config match [OPTIONS] REGEX [REGEX ...]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command.add_command("reload", "Request a configuration reload", [&]() { engine.config_reload(); })
    .add_example_usage("traffic_ctl config reload");
  config_command.add_command("status", "Check the configuration status", [&]() { engine.config_status(); })
    .add_example_usage("traffic_ctl config status");
  config_command.add_command("set", "Set a configuration value", "", 2, [&]() { engine.config_set(); })
    .add_example_usage("traffic_ctl config set RECORD VALUE");

  // host commands
  host_command.add_command("status", "Get one or more host statuses", "", MORE_THAN_ONE_ARG_N, [&]() { engine.status_get(); })
    .add_example_usage("traffic_ctl host status HOST  [HOST  ...]");
  host_command.add_command("down", "Set down one or more host(s)", "", MORE_THAN_ONE_ARG_N, [&]() { engine.status_down(); })
    .add_example_usage("traffic_ctl host down HOST [OPTIONS]")
    .add_option("--time", "-I", "number of seconds that a host is marked down", "", 1)
    .add_option("--reason", "", "reason for marking the host down, one of 'manual|active|local", "", 1);
  host_command.add_command("up", "Set up one or more host(s)", "", MORE_THAN_ONE_ARG_N, [&]() { engine.status_up(); })
    .add_example_usage("traffic_ctl host up METRIC value")
    .add_option("--reason", "", "reason for marking the host up, one of 'manual|active|local", "", 1);

  // metric commands
  metric_command.add_command("get", "Get one or more metric values", "", MORE_THAN_ONE_ARG_N, [&]() { engine.metric_get(); })
    .add_example_usage("traffic_ctl metric get METRIC [METRIC ...]");
  metric_command.add_command("clear", "Clear all metric values", [&]() { engine.metric_clear(); });
  metric_command.add_command("describe", "Show detailed information about one or more metric values", "", MORE_THAN_ONE_ARG_N,
                             [&]() { engine.CtrlUnimplementedCommand("describe"); }); // not implemented
  metric_command.add_command("match", "Get metrics matching a regular expression", "", MORE_THAN_ZERO_ARG_N,
                             [&]() { engine.metric_match(); });
  metric_command.add_command("monitor", "Display the value of a metric over time", "", MORE_THAN_ZERO_ARG_N,
                             [&]() { engine.CtrlUnimplementedCommand("monitor"); }); // not implemented
  metric_command.add_command("zero", "Clear one or more metric values", "", MORE_THAN_ONE_ARG_N, [&]() { engine.metric_zero(); });

  // plugin command
  plugin_command
    .add_command("msg", "Send message to plugins - a TAG and the message DATA(optional)", "", MORE_THAN_ONE_ARG_N,
                 [&]() { engine.plugin_msg(); })
    .add_example_usage("traffic_ctl plugin msg TAG DATA");

  // server commands
  server_command.add_command("backtrace", "Show a full stack trace of the traffic_server process",
                             [&]() { engine.server_backtrace(); });
  server_command.add_command("restart", "Restart Traffic Server", [&]() { engine.server_restart(); })
    .add_example_usage("traffic_ctl server restart [OPTIONS]")
    .add_option("--drain", "", "Wait for client connections to drain before restarting")
    .add_option("--manager", "", "Restart traffic_manager as well as traffic_server");
  server_command.add_command("start", "Start the proxy", [&]() { engine.server_start(); })
    .add_example_usage("traffic_ctl server start [OPTIONS]")
    .add_option("--clear-cache", "", "Clear the disk cache on startup")
    .add_option("--clear-hostdb", "", "Clear the DNS cache on startup");
  server_command.add_command("status", "Show the proxy status", [&]() { engine.server_status(); })
    .add_example_usage("traffic_ctl server status");
  server_command.add_command("stop", "Stop the proxy", [&]() { engine.server_stop(); })
    .add_example_usage("traffic_ctl server stop [OPTIONS]")
    .add_option("--drain", "", "Wait for client connections to drain before stopping");
  server_command.add_command("drain", "Drain the requests", [&]() { engine.server_drain(); })
    .add_example_usage("traffic_ctl server drain [OPTIONS]")
    .add_option("--no-new-connection", "-N", "Wait for new connections down to threshold before starting draining")
    .add_option("--undo", "-U", "Recover server from the drain mode");

  // storage commands
  storage_command
    .add_command("offline", "Take one or more storage volumes offline", "", MORE_THAN_ONE_ARG_N,
                 [&]() { engine.storage_offline(); })
    .add_example_usage("storage offline DEVICE [DEVICE ...]");
  storage_command.add_command("status", "Show the storage configuration", "", MORE_THAN_ZERO_ARG_N,
                              [&]() { engine.CtrlUnimplementedCommand("status"); }); // not implemented

  // parse the arguments
  engine.arguments = engine.parser.parse(argv);

  BaseLogFile *base_log_file = new BaseLogFile("stderr");
  diags                      = new Diags("traffic_ctl", "" /* tags */, "" /* actions */, base_log_file);

  if (engine.arguments.get("debug")) {
    diags->activate_taglist("traffic_ctl", DiagsTagType_Debug);
    diags->config.enabled[DiagsTagType_Debug] = true;
    diags->show_location                      = SHOW_LOCATION_DEBUG;
  }

  argparser_runroot_handler(engine.arguments.get("run-root").value(), argv[0]);
  Layout::create();

  // This is a little bit of a hack, for now it'll suffice.
  max_records_entries = 262144;
  RecProcessInit(RECM_STAND_ALONE, diags);
  LibRecordsConfigInit();

  ats_scoped_str rundir(RecConfigReadRuntimeDir());

  // Make a best effort to connect the control socket. If it turns out we are
  // just displaying help or something then it
  // doesn't matter that we failed. If we end up performing some operation then
  // that operation will fail and display the
  // error.
  TSInit(rundir, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));

  engine.arguments.invoke();

  // Done with the mgmt API.
  TSTerminate();

  return engine.status_code;
}
