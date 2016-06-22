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

AppVersionInfo CtrlVersionInfo;

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

int
CtrlSubcommandUsage(const char *name, const subcommand *cmds, unsigned ncmds, const ArgumentDescription *desc, unsigned ndesc)
{
  const char *opt = ndesc ? "[OPTIONS]" : "";
  const char *sep = (ndesc && name) ? " " : "";

  fprintf(stderr, "Usage: traffic_ctl %s%s%s CMD [ARGS ...]\n\nSubcommands:\n", name ? name : "", sep, opt);

  for (unsigned i = 0; i < ncmds; ++i) {
    fprintf(stderr, "    %-16s%s\n", cmds[i].name, cmds[i].help);
  }

  if (ndesc) {
    usage(desc, ndesc, "\nOptions:");
  }

  return CTRL_EX_USAGE;
}

int
CtrlCommandUsage(const char *msg, const ArgumentDescription *desc, unsigned ndesc)
{
  fprintf(stderr, "Usage: traffic_ctl %s\n", msg);

  if (ndesc) {
    usage(desc, ndesc, "\nOptions:");
  }

  return CTRL_EX_USAGE;
}

bool
CtrlProcessArguments(int /* argc */, const char **argv, const ArgumentDescription *desc, unsigned ndesc)
{
  n_file_arguments = 0;
  return process_args_ex(&CtrlVersionInfo, desc, ndesc, argv);
}

int
CtrlUnimplementedCommand(unsigned /* argc */, const char **argv)
{
  CtrlDebug("the '%s' command is not implemented", *argv);
  return CTRL_EX_UNIMPLEMENTED;
}

int
CtrlGenericSubcommand(const char *name, const subcommand *cmds, unsigned ncmds, unsigned argc, const char **argv)
{
  CtrlCommandLine cmdline;

  // Process command line arguments and dump into variables
  if (!CtrlProcessArguments(argc, argv, NULL, 0) || n_file_arguments < 1) {
    return CtrlSubcommandUsage(name, cmds, ncmds, NULL, 0);
  }

  cmdline.init(n_file_arguments, file_arguments);

  for (unsigned i = 0; i < ncmds; ++i) {
    if (strcmp(file_arguments[0], cmds[i].name) == 0) {
      return cmds[i].handler(cmdline.argc(), cmdline.argv());
    }
  }

  return CtrlSubcommandUsage(name, cmds, ncmds, NULL, 0);
}

int
main(int argc, const char **argv)
{
  CtrlCommandLine cmdline;
  int debug = false;

  CtrlVersionInfo.setup(PACKAGE_NAME, "traffic_ctl", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  program_name = CtrlVersionInfo.AppStr;

  ArgumentDescription argument_descriptions[] = {{"debug", '-', "Enable debugging output", "F", &debug, NULL, NULL},
                                                 HELP_ARGUMENT_DESCRIPTION(),
                                                 VERSION_ARGUMENT_DESCRIPTION()};

  const subcommand commands[] = {
    {subcommand_alarm, "alarm", "Manipulate alarms"},
    {subcommand_cluster, "cluster", "Stop, restart and examine the cluster"},
    {subcommand_config, "config", "Manipulate configuration records"},
    {subcommand_metric, "metric", "Manipulate performance metrics"},
    {subcommand_server, "server", "Stop, restart and examine the server"},
    {subcommand_storage, "storage", "Manipulate cache storage"},
  };

  BaseLogFile *base_log_file = new BaseLogFile("stderr");
  diags                      = new Diags("" /* tags */, "" /* actions */, base_log_file);

  // Process command line arguments and dump into variables
  if (!CtrlProcessArguments(argc, argv, argument_descriptions, countof(argument_descriptions))) {
    return CtrlSubcommandUsage(NULL, commands, countof(commands), argument_descriptions, countof(argument_descriptions));
  }

  if (debug) {
    diags->activate_taglist("traffic_ctl", DiagsTagType_Debug);
    diags->config.enabled[DiagsTagType_Debug] = true;
    diags->show_location                      = true;
  }

  CtrlDebug("debug logging active");

  if (n_file_arguments < 1) {
    return CtrlSubcommandUsage(NULL, commands, countof(commands), argument_descriptions, countof(argument_descriptions));
  }

  // Make a best effort to connect the control socket. If it turns out we are just displaying help or something then it
  // doesn't matter that we failed. If we end up performing some operation then that operation will fail and display the
  // error.
  TSInit(NULL, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));

  for (unsigned i = 0; i < countof(commands); ++i) {
    if (strcmp(file_arguments[0], commands[i].name) == 0) {
      CtrlCommandLine cmdline;

      cmdline.init(n_file_arguments, file_arguments);
      return commands[i].handler(cmdline.argc(), cmdline.argv());
    }
  }

  // Done with the mgmt API.
  TSTerminate();
  return CtrlSubcommandUsage(NULL, commands, countof(commands), argument_descriptions, countof(argument_descriptions));
}
