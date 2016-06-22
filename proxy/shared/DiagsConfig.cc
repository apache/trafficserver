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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_file.h"
#include "ts/I_Layout.h"
#include "DiagsConfig.h"
#include "P_RecCore.h"

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
  } output_records[] = {{"proxy.config.diags.output.diag", DL_Diag},           {"proxy.config.diags.output.debug", DL_Debug},
                        {"proxy.config.diags.output.status", DL_Status},       {"proxy.config.diags.output.note", DL_Note},
                        {"proxy.config.diags.output.warning", DL_Warning},     {"proxy.config.diags.output.error", DL_Error},
                        {"proxy.config.diags.output.fatal", DL_Fatal},         {"proxy.config.diags.output.alert", DL_Alert},
                        {"proxy.config.diags.output.emergency", DL_Emergency}, {NULL, DL_Undefined}};

  if (!callbacks_established) {
    register_diags_callbacks();
  }
  ////////////////////////////////////////////
  // extract relevant records.config values //
  ////////////////////////////////////////////

  all_found = true;

  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug]  = (diags->base_debug_tags != NULL);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != NULL);

  // enabled if records.config set

  e = (int)REC_readInteger("proxy.config.diags.debug.enabled", &found);
  if (e && found)
    c.enabled[DiagsTagType_Debug] = 1; // implement OR logic
  all_found                       = all_found && found;

  e = (int)REC_readInteger("proxy.config.diags.action.enabled", &found);
  if (e && found)
    c.enabled[DiagsTagType_Action] = 1; // implement OR logic
  all_found                        = all_found && found;

  e                    = (int)REC_readInteger("proxy.config.diags.show_location", &found);
  diags->show_location = ((e && found) ? 1 : 0);
  all_found            = all_found && found;

  // read output routing values
  for (i = 0;; i++) {
    const char *record_name = output_records[i].config_name;
    DiagsLevel l            = output_records[i].level;

    if (!record_name)
      break;
    p         = REC_readString(record_name, &found);
    all_found = all_found && found;
    if (found) {
      parse_output_string(p, &(c.outputs[l]));
      ats_free(p);
    } else {
      diags->print(NULL, DTA(DL_Error), "can't find config variable '%s'\n", record_name);
    }
  }

  p         = REC_readString("proxy.config.diags.debug.tags", &found);
  dt        = (found ? p : NULL); // NOTE: needs to be freed
  all_found = all_found && found;

  p         = REC_readString("proxy.config.diags.action.tags", &found);
  at        = (found ? p : NULL); // NOTE: needs to be freed
  all_found = all_found && found;

  ///////////////////////////////////////////////////////////////////
  // if couldn't read all values, return without changing config,  //
  // otherwise rebuild taglists and change the diags config values //
  ///////////////////////////////////////////////////////////////////

  if (!all_found) {
    diags->print(NULL, DTA(DL_Error), "couldn't fetch all proxy.config.diags values");
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
    diags->print(NULL, DTA(DL_Note), "updated diags config");
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

  diagsConfig = (DiagsConfig *)opaque_token;
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
    c.enabled[DiagsTagType_Debug] = 1;
  } else {
    c.enabled[DiagsTagType_Debug] = 0;
  }

  if (diags->base_action_tags) {
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);
    c.enabled[DiagsTagType_Action] = 1;
  } else {
    c.enabled[DiagsTagType_Action] = 0;
  }

#if !defined(__GNUC__)
  diags->config = c;
#else
  memcpy(((void *)&diags->config), ((void *)&c), sizeof(DiagsConfigState));
#endif
}

void
DiagsConfig::RegisterDiagConfig()
{
  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.diags.debug.enabled", 0, RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.debug.tags", "", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.diags.action.enabled", 0, RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.action.tags", "", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.diags.show_location", 0, RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.diag", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.debug", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.status", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.note", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.warning", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.error", "SL", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.fatal", "SL", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.alert", "L", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
  RecRegisterConfigString(RECT_CONFIG, "proxy.config.diags.output.emergency", "SL", RECU_NULL, RECC_NULL, NULL, REC_SOURCE_DEFAULT);
}

DiagsConfig::DiagsConfig(const char *filename, const char *tags, const char *actions, bool use_records) : diags_log(NULL)
{
  char diags_logpath[PATH_NAME_MAX];
  ats_scoped_str logpath;

  callbacks_established = false;
  diags                 = NULL;

  ////////////////////////////////////////////////////////////////////
  //  If we aren't using the manager records for configuation       //
  //   just build the tables based on command line parameters and   //
  //   exit                                                         //
  ////////////////////////////////////////////////////////////////////

  if (!use_records) {
    diags = new Diags(tags, actions, NULL);
    config_diags_norecords();
    return;
  }

  // Open the diagnostics log. If proxy.config.log.logfile_dir is set use that, otherwise fall
  // back to the configured log directory.

  logpath = RecConfigReadLogDir();
  if (access(logpath, W_OK | R_OK) == -1) {
    fprintf(stderr, "unable to access log directory '%s': %d, %s\n", (const char *)logpath, errno, strerror(errno));
    fprintf(stderr, "please set 'proxy.config.log.logfile_dir'\n");
    _exit(1);
  }

  ink_filepath_make(diags_logpath, sizeof(diags_logpath), logpath, filename);

  // Grab rolling intervals from configuration
  // TODO error check these values
  int output_log_roll_int    = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_interval_sec");
  int output_log_roll_size   = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_size_mb");
  int output_log_roll_enable = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_enabled");
  int diags_log_roll_int     = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_interval_sec");
  int diags_log_roll_size    = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_size_mb");
  int diags_log_roll_enable  = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_enabled");

  // Set up diags, FILE streams are opened in Diags constructor
  diags_log = new BaseLogFile(diags_logpath);
  diags     = new Diags(tags, actions, diags_log);
  diags->config_roll_diagslog((RollingEnabledValues)diags_log_roll_enable, diags_log_roll_int, diags_log_roll_size);
  diags->config_roll_outputlog((RollingEnabledValues)output_log_roll_enable, output_log_roll_int, output_log_roll_size);

  diags->print(NULL, DTA(DL_Status), "opened %s", diags_logpath);

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
    "proxy.config.diags.output.alert",   "proxy.config.diags.output.emergency", NULL};

  bool total_status = true;
  bool status;
  int i;
  void *o = (void *)this;

  // set triggers to call same callback for any diag config change
  for (i = 0; config_record_names[i] != NULL; i++) {
    status = (REC_RegisterConfigUpdateFunc(config_record_names[i], diags_config_callback, o) == REC_ERR_OKAY);
    if (!status) {
      diags->print(NULL, DTA(DL_Warning), "couldn't register variable '%s', is records.config up to date?", config_record_names[i]);
    }
    total_status = total_status && status;
  }

  if (total_status == false) {
    diags->print(NULL, DTA(DL_Error), "couldn't setup all diags callbacks, diagnostics may misbehave");
    callbacks_established = false;
  } else {
    callbacks_established = true;
  }
}

DiagsConfig::~DiagsConfig()
{
  delete diags;
}
