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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_file.h"
#include "tscore/I_Layout.h"
#include "tscore/Filenames.h"
#include "DiagsConfig.h"
#include "records/P_RecCore.h"

//////////////////////////////////////////////////////////////////////////////
//
//      void reconfigure_diags()
//
//      This function extracts the current diags configuration settings from
//      records.config, and rebuilds the Diags data structures.
//
//////////////////////////////////////////////////////////////////////////////

void
DiagsConfig::reconfigure_diags()
{
  int i, e;
  char *p, *dt, *at;
  DiagsConfigState c;
  bool found, all_found;

  static struct {
    const char *config_name;
    DiagsLevel level;
  } output_records[] = {
    {"proxy.config.diags.output.diag", DL_Diag},           {"proxy.config.diags.output.debug", DL_Debug},
    {"proxy.config.diags.output.status", DL_Status},       {"proxy.config.diags.output.note", DL_Note},
    {"proxy.config.diags.output.warning", DL_Warning},     {"proxy.config.diags.output.error", DL_Error},
    {"proxy.config.diags.output.fatal", DL_Fatal},         {"proxy.config.diags.output.alert", DL_Alert},
    {"proxy.config.diags.output.emergency", DL_Emergency}, {nullptr, DL_Undefined},
  };

  if (!callbacks_established) {
    register_diags_callbacks();
  }
  ////////////////////////////////////////////
  // extract relevant records.config values //
  ////////////////////////////////////////////

  all_found = true;

  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug]  = (diags->base_debug_tags != nullptr);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != nullptr);

  // enabled if records.config set

  e = static_cast<int>(REC_readInteger("proxy.config.diags.debug.enabled", &found));
  if (e && found) {
    c.enabled[DiagsTagType_Debug] = e; // implement OR logic
  }
  all_found = all_found && found;

  e = static_cast<int>(REC_readInteger("proxy.config.diags.action.enabled", &found));
  if (e && found) {
    c.enabled[DiagsTagType_Action] = true; // implement OR logic
  }
  all_found = all_found && found;

  e                    = static_cast<int>(REC_readInteger("proxy.config.diags.show_location", &found));
  diags->show_location = ((e == 1 && found) ? SHOW_LOCATION_DEBUG : ((e == 2 && found) ? SHOW_LOCATION_ALL : SHOW_LOCATION_NONE));
  all_found            = all_found && found;

  // read output routing values
  for (i = 0;; i++) {
    const char *record_name = output_records[i].config_name;
    DiagsLevel l            = output_records[i].level;

    if (!record_name) {
      break;
    }

    p         = REC_readString(record_name, &found);
    all_found = all_found && found;

    if (found) {
      parse_output_string(p, &(c.outputs[l]));
      ats_free(p);
    } else {
      Error("can't find config variable '%s'", record_name);
    }
  }

  p         = REC_readString("proxy.config.diags.debug.tags", &found);
  dt        = (found ? p : nullptr); // NOTE: needs to be freed
  all_found = all_found && found;

  p         = REC_readString("proxy.config.diags.action.tags", &found);
  at        = (found ? p : nullptr); // NOTE: needs to be freed
  all_found = all_found && found;

  ///////////////////////////////////////////////////////////////////
  // if couldn't read all values, return without changing config,  //
  // otherwise rebuild taglists and change the diags config values //
  ///////////////////////////////////////////////////////////////////

  if (!all_found) {
    Error("couldn't fetch all proxy.config.diags values");
  } else {
    //////////////////////////////
    // clear out old tag tables //
    //////////////////////////////

    diags->deactivate_all(DiagsTagType_Debug);
    diags->deactivate_all(DiagsTagType_Action);

    //////////////////////////////////////////////////////////////////////
    // add new tag tables from records.config or command line overrides //
    //////////////////////////////////////////////////////////////////////

    diags->activate_taglist((diags->base_debug_tags ? diags->base_debug_tags : dt), DiagsTagType_Debug);
    diags->activate_taglist((diags->base_action_tags ? diags->base_action_tags : at), DiagsTagType_Action);

////////////////////////////////////
// change the diags config values //
////////////////////////////////////
#if !defined(__GNUC__)
    diags->config = c;
#else
    memcpy(((void *)&diags->config), ((void *)&c), sizeof(DiagsConfigState));
#endif
    Note("updated diags config");
  }

  ////////////////////////////////////
  // free the record.config strings //
  ////////////////////////////////////
  ats_free(dt);
  ats_free(at);
}

//////////////////////////////////////////////////////////////////////////////
//
//      static void *diags_config_callback(void *opaque_token, void *data)
//
//      This is the records.config registration callback that is called
//      when any diags value is changed.  Each time a diags value changes
//      the entire diags state is reconfigured.
//
//////////////////////////////////////////////////////////////////////////////
static int
diags_config_callback(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData /* data ATS_UNUSED */,
                      void *opaque_token)
{
  DiagsConfig *diagsConfig;

  diagsConfig = static_cast<DiagsConfig *>(opaque_token);
  ink_assert(diags->magic == DIAGS_MAGIC);
  diagsConfig->reconfigure_diags();
  return (0);
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::parse_output_string(char *s, DiagsModeOutput *o)
//
//      This routine converts a diags outpur routing string <s> to the
//      internal DiagsModeOutput structure.  Currently there are 4 possible
//      routing destinations:
//              O  stdout
//              E  stderr
//              S  syslog
//              L  diags.log
//
//////////////////////////////////////////////////////////////////////////////

void
DiagsConfig::parse_output_string(char *s, DiagsModeOutput *o)
{
  o->to_stdout   = (s && strchr(s, 'O'));
  o->to_stderr   = (s && strchr(s, 'E'));
  o->to_syslog   = (s && strchr(s, 'S'));
  o->to_diagslog = (s && strchr(s, 'L'));
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::config_norecords()
//
//      Builds the Diags data structures based on the command line values
//        it does not use any of the records based config variables
//
//////////////////////////////////////////////////////////////////////////////
void
DiagsConfig::config_diags_norecords()
{
  DiagsConfigState c;
  ink_zero(c);

  //////////////////////////////
  // clear out old tag tables //
  //////////////////////////////
  diags->deactivate_all(DiagsTagType_Debug);
  diags->deactivate_all(DiagsTagType_Action);

  //////////////////////////////////////////////////////////////////////
  // add new tag tables from command line overrides only              //
  //////////////////////////////////////////////////////////////////////

  if (diags->base_debug_tags) {
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
    c.enabled[DiagsTagType_Debug] = true;
  } else {
    c.enabled[DiagsTagType_Debug] = false;
  }

  if (diags->base_action_tags) {
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);
    c.enabled[DiagsTagType_Action] = true;
  } else {
    c.enabled[DiagsTagType_Action] = false;
  }

#if !defined(__GNUC__)
  diags->config = c;
#else
  memcpy(((void *)&diags->config), ((void *)&c), sizeof(DiagsConfigState));
#endif
}

DiagsConfig::DiagsConfig(std::string_view prefix_string, const char *filename, const char *tags, const char *actions,
                         bool use_records)
  : callbacks_established(false), diags_log(nullptr), diags(nullptr)
{
  char diags_logpath[PATH_NAME_MAX];
  ats_scoped_str logpath;

  ////////////////////////////////////////////////////////////////////
  //  If we aren't using the manager records for configuration      //
  //   just build the tables based on command line parameters and   //
  //   exit                                                         //
  ////////////////////////////////////////////////////////////////////

  if (!use_records) {
    diags = new Diags(prefix_string, tags, actions, nullptr);
    config_diags_norecords();
    return;
  }

  // Open the diagnostics log. If proxy.config.log.logfile_dir is set use that, otherwise fall
  // back to the configured log directory.

  logpath = RecConfigReadLogDir();
  if (access(logpath, W_OK | R_OK) == -1) {
    fprintf(stderr, "unable to access log directory '%s': %d, %s\n", (const char *)logpath, errno, strerror(errno));
    fprintf(stderr, "please set 'proxy.config.log.logfile_dir'\n");
    ::exit(1);
  }

  ink_filepath_make(diags_logpath, sizeof(diags_logpath), logpath, filename);

  // Grab rolling intervals from configuration
  // TODO error check these values
  int output_log_roll_int    = static_cast<int>(REC_ConfigReadInteger("proxy.config.output.logfile.rolling_interval_sec"));
  int output_log_roll_size   = static_cast<int>(REC_ConfigReadInteger("proxy.config.output.logfile.rolling_size_mb"));
  int output_log_roll_enable = static_cast<int>(REC_ConfigReadInteger("proxy.config.output.logfile.rolling_enabled"));
  int diags_log_roll_int     = static_cast<int>(REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_interval_sec"));
  int diags_log_roll_size    = static_cast<int>(REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_size_mb"));
  int diags_log_roll_enable  = static_cast<int>(REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_enabled"));

  // Grab some perms for the actual files on disk
  char *diags_perm       = REC_ConfigReadString("proxy.config.diags.logfile_perm");
  char *output_perm      = REC_ConfigReadString("proxy.config.output.logfile_perm");
  int diags_perm_parsed  = diags_perm ? ink_fileperm_parse(diags_perm) : -1;
  int output_perm_parsed = diags_perm ? ink_fileperm_parse(output_perm) : -1;

  ats_free(diags_perm);
  ats_free(output_perm);

  // Set up diags, FILE streams are opened in Diags constructor
  diags_log = new BaseLogFile(diags_logpath);
  diags     = new Diags(prefix_string, tags, actions, diags_log, diags_perm_parsed, output_perm_parsed);
  diags->config_roll_diagslog(static_cast<RollingEnabledValues>(diags_log_roll_enable), diags_log_roll_int, diags_log_roll_size);
  diags->config_roll_outputlog(static_cast<RollingEnabledValues>(output_log_roll_enable), output_log_roll_int,
                               output_log_roll_size);

  Status("opened %s", diags_logpath);

  register_diags_callbacks();

  reconfigure_diags();
}

//////////////////////////////////////////////////////////////////////////////
//
//      void DiagsConfig::register_diags_callbacks()
//
//      set up management callbacks to update diags on every change ---   //
//      right now, this system kind of sucks, we rebuild the tag tables //
//      from scratch for *every* proxy.config.diags value that changed; //
//      dgourley is looking into changing the management API to provide //
//      a callback each time records.config changed, possibly better.   //
//
//////////////////////////////////////////////////////////////////////////////
void
DiagsConfig::register_diags_callbacks()
{
  static const char *config_record_names[] = {
    "proxy.config.diags.debug.enabled",  "proxy.config.diags.debug.tags",       "proxy.config.diags.action.enabled",
    "proxy.config.diags.action.tags",    "proxy.config.diags.show_location",    "proxy.config.diags.output.diag",
    "proxy.config.diags.output.debug",   "proxy.config.diags.output.status",    "proxy.config.diags.output.note",
    "proxy.config.diags.output.warning", "proxy.config.diags.output.error",     "proxy.config.diags.output.fatal",
    "proxy.config.diags.output.alert",   "proxy.config.diags.output.emergency", nullptr,
  };

  bool total_status = true;
  bool status;
  int i;
  void *o = (void *)this;

  // set triggers to call same callback for any diag config change
  for (i = 0; config_record_names[i] != nullptr; i++) {
    status = (REC_RegisterConfigUpdateFunc(config_record_names[i], diags_config_callback, o) == REC_ERR_OKAY);
    if (!status) {
      Warning("couldn't register variable '%s', is %s up to date?", config_record_names[i], ts::filename::RECORDS);
    }
    total_status = total_status && status;
  }

  if (total_status == false) {
    Error("couldn't setup all diags callbacks, diagnostics may misbehave");
    callbacks_established = false;
  } else {
    callbacks_established = true;
  }
}

DiagsConfig::~DiagsConfig()
{
  delete diags;
}
