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


// Diags *diags;
#define DIAGS_LOG_FILE "diags.log"

//////////////////////////////////////////////////////////////////////////////
//
//      void reconfigure_diags()
//
//      This function extracts the current diags configuration settings from
//      records.config, and rebuilds the Diags data structures.
//
//////////////////////////////////////////////////////////////////////////////

static void
reconfigure_diags()
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
  memcpy(((void *)&diags->config), ((void *)&c), sizeof(DiagsConfigState));
#endif
}


static void
init_diags(const char *bdt, const char *bat)
{
  char diags_logpath[500];
  strcpy(diags_logpath, DIAGS_LOG_FILE);

  diags = new Diags(bdt, bat, new BaseLogFile(diags_logpath));
  Status("opened %s", diags_logpath);

  reconfigure_diags();
}
