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

/****************************************************************************

   Initialize.cc --
   Created On      : Fri Feb  5 18:22:05 1999

   Description:
 ****************************************************************************/
#include "libts.h"

#include "Diags.h"
#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_Layout.h"
#include "I_Version.h"

#include "Initialize.h" // TODO: move to I_Initialize.h ???


//
// Initialize operating system related information/services
//
void
init_system_settings(void)
{
  // Delimit file Descriptors
  fds_limit = ink_max_out_rlimit(RLIMIT_NOFILE, true, false);

  ink_max_out_rlimit(RLIMIT_STACK,true,true);
  ink_max_out_rlimit(RLIMIT_DATA,true,true);
  ink_max_out_rlimit(RLIMIT_FSIZE, true, false);
#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS,true,true);
#endif
}

#if defined(linux)
#include <sys/prctl.h>
#endif

static int
set_core_size(const char *name, RecDataT data_type, RecData data, void *opaque_token)
{

  RecInt size = data.rec_int;
  struct rlimit lim;
  bool failed = false;

  NOWARN_UNUSED(opaque_token);

  if (getrlimit(RLIMIT_CORE, &lim) < 0) {
    failed = true;
  } else {
    if (size < 0) {
      lim.rlim_cur = lim.rlim_max;
    } else {
      lim.rlim_cur = (rlim_t) size;
    }
    if (setrlimit(RLIMIT_CORE, &lim) < 0) {
      failed = true;
    }
#if defined(linux)
#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4
#endif
    if (size != 0)
      prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif
  }

  if (failed == true) {
    Warning("Failed to set Core Limit : %s", strerror(errno));
  }
  return 0;
}

void
init_system_core_size(void)
{
  bool found;
  RecInt coreSize;
  found = (RecGetRecordInt("proxy.config.core_limit", &coreSize) == REC_ERR_OKAY);
  if (found == false) {
    Warning("Unable to determine core limit");
  } else {
    RecData rec_temp;
    rec_temp.rec_int = coreSize;
    set_core_size(NULL, RECD_INT, rec_temp, NULL);
    found = (REC_RegisterConfigUpdateFunc("proxy.config.core_limit", set_core_size, NULL) == REC_ERR_OKAY);
    ink_assert(found);
  }
}


int system_syslog_facility = LOG_DAEMON;

//   Reads the syslog configuration variable
//     and sets the global integer for the
//     facility and calls open log with the
//     new facility
void
init_system_syslog_log_configure(void)
{
  char *facility_str = NULL;
  int facility;

  REC_ReadConfigStringAlloc(facility_str, "proxy.config.syslog_facility");

  if (facility_str == NULL || (facility = facility_string_to_int(facility_str)) < 0) {
    syslog(LOG_WARNING, "Bad or missing syslog facility.  " "Defaulting to LOG_DAEMON");
  } else {
    system_syslog_facility = facility;
    closelog();
    openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
  }
}


void
init_system_adjust_num_of_net_threads(void)
{
  float autoconfig_scale = 1.0;
  int nth_auto_config = 1;
  int num_of_threads_tmp = 1;

  REC_ReadConfigInteger(nth_auto_config, "proxy.config.exec_thread.autoconfig");
  if (!nth_auto_config) {
    REC_ReadConfigInteger(num_of_threads_tmp, "proxy.config.exec_thread.limit");
    if (num_of_threads_tmp <= 0)
      num_of_threads_tmp = 1;
    else if (num_of_threads_tmp > MAX_NUMBER_OF_THREADS)
      num_of_threads_tmp = MAX_NUMBER_OF_THREADS;
    system_num_of_net_threads = num_of_threads_tmp;
    if (is_debug_tag_set("threads")) {
      fprintf(stderr, "# net threads Auto config - disabled - use config file settings\n");
    }
  } else {                      /* autoconfig is enabled */
    num_of_threads_tmp = system_num_of_net_threads;
    REC_ReadConfigFloat(autoconfig_scale, "proxy.config.exec_thread.autoconfig.scale");
    num_of_threads_tmp = (int) ((float) num_of_threads_tmp * autoconfig_scale);
    if (num_of_threads_tmp) {
      system_num_of_net_threads = num_of_threads_tmp;
    }
    if (unlikely(num_of_threads_tmp > MAX_NUMBER_OF_THREADS)) {
      num_of_threads_tmp = MAX_NUMBER_OF_THREADS;
    }
    if (is_debug_tag_set("threads")) {
      fprintf(stderr, "# net threads Auto config - enabled\n");
      fprintf(stderr, "# autoconfig scale: %f\n", autoconfig_scale);
      fprintf(stderr, "# scaled number of net threads: %d\n", num_of_threads_tmp);
    }
  }

  if (is_debug_tag_set("threads")) {
    fprintf(stderr, "# number of net threads: %d\n", system_num_of_net_threads);
  }
  if (unlikely(system_num_of_net_threads <= 0)) {      /* impossible case -just for protection */
    Warning("Number of Net Threads should be greater than 0");
    system_num_of_net_threads = 1;
  }
}


//////////////////////////////////////////////////////////////////////////////
//
//      void reconfigure_diags()
//
//      This function extracts the current diags configuration settings from
//      records.config, and rebuilds the Diags data structures.
//
//////////////////////////////////////////////////////////////////////////////
void
init_system_reconfigure_diags(void)
{
  int i;
  DiagsConfigState c;

  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug] = (diags->base_debug_tags != NULL);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != NULL);

  c.enabled[DiagsTagType_Debug] = 1;
  c.enabled[DiagsTagType_Action] = 1;
  diags->show_location = 1;

  // read output routing values
  for (i = 0; i < DiagsLevel_Count; i++) {

    c.outputs[i].to_stdout = 0;
    c.outputs[i].to_stderr = 1;
    c.outputs[i].to_syslog = 1;
    c.outputs[i].to_diagslog = 1;
  }

  //////////////////////////////
  // clear out old tag tables //
  //////////////////////////////
  diags->deactivate_all(DiagsTagType_Debug);
  diags->deactivate_all(DiagsTagType_Action);

  //////////////////////////////////////////////////////////////////////
  //                     add new tag tables
  //////////////////////////////////////////////////////////////////////
  if (diags->base_debug_tags)
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  if (diags->base_action_tags)
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);

  ////////////////////////////////////
  // change the diags config values //
  ////////////////////////////////////
#if !defined(__GNUC__) && !defined(hpux)
  diags->config = c;
#else
  memcpy(((void *) &diags->config), ((void *) &c), sizeof(DiagsConfigState));
#endif

}


void
init_system_diags(char *bdt, char *bat)
{
  FILE *diags_log_fp = NULL;
  char diags_logpath[PATH_NAME_MAX + 1];

  ink_filepath_make(diags_logpath, sizeof(diags_logpath),
                    system_log_dir, DIAGS_LOG_FILE);

  diags_log_fp = fopen(diags_logpath, "w");
  if (diags_log_fp) {
    int status;
    chown_file_to_user(diag_logpath,admin_user);
    status = setvbuf(diags_log_fp, NULL, _IOLBF, 512);
    if (status != 0) {
      fclose(diags_log_fp);
      diags_log_fp = NULL;
    }
  }

  diags = NEW(new Diags(bdt, bat, diags_log_fp));
  if (diags_log_fp == NULL) {
    SrcLoc loc(__FILE__, __FUNCTION__, __LINE__);

    diags->print(NULL, DL_Warning, NULL, &loc,
                 "couldn't open diags log file '%s', " "will not log to this file", diags_logpath);
  } else {
    diags->print(NULL, DL_Status, "STATUS", NULL, "opened %s", diags_logpath);
  }

  init_system_reconfigure_diags();
}

