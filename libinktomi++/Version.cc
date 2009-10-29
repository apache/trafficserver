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

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

#include "inktomi++.h"

AppVersionInfo::AppVersionInfo()
{
  defined = 0;

  ink_strncpy(AppStr, "?", sizeof(AppStr));
  ink_strncpy(VersionStr, "?", sizeof(VersionStr));
  ink_strncpy(BldNumStr, "?", sizeof(BldNumStr));
  ink_strncpy(BldTimeStr, "?", sizeof(BldTimeStr));
  ink_strncpy(BldDateStr, "?", sizeof(BldDateStr));
  ink_strncpy(BldMachineStr, "?", sizeof(BldMachineStr));
  ink_strncpy(BldPersonStr, "?", sizeof(BldPersonStr));
  ink_strncpy(BldCompileFlagsStr, "?", sizeof(BldCompileFlagsStr));
  ink_strncpy(FullVersionInfoStr, "?", sizeof(FullVersionInfoStr));
  // coverity[uninit_member]
}


void
AppVersionInfo::setup(char *app_name, char *app_version,
                      char *build_date, char *build_time, char *build_machine, char *build_person, char *build_cflags)
{
  char month_name[8];
  int year, month, day, hour, minute, second;

  static const char *months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "???"
  };

  // coverity[secure_coding]
  sscanf(build_time, "%d:%d:%d", &hour, &minute, &second);
  // coverity[secure_coding]
  sscanf(build_date, "%3s %d %d", month_name, &day, &year);

  for (month = 0; month < 11; month++) {
    if (strcasecmp(months[month], month_name) == 0)
      break;
  }

  ///////////////////////////////////////////
  // now construct the version information //
  ///////////////////////////////////////////

  ink_strncpy(AppStr, app_name, sizeof(AppStr));
  snprintf(VersionStr, sizeof(VersionStr), "%s", app_version);
  snprintf(BldNumStr, sizeof(BldNumStr), "%d%d%d", month, day, hour);
  snprintf(BldTimeStr, sizeof(BldTimeStr), "%s", build_time);
  snprintf(BldDateStr, sizeof(BldDateStr), "%s", build_date);
  snprintf(BldMachineStr, sizeof(BldMachineStr), "%s", build_machine);
  snprintf(BldPersonStr, sizeof(BldPersonStr), "%s", build_person);
  snprintf(BldCompileFlagsStr, sizeof(BldCompileFlagsStr), "%s", build_cflags);

  snprintf(FullVersionInfoStr, sizeof(FullVersionInfoStr), "%s %s - (build # %d%d%d on %s at %s)",
           AppStr, VersionStr, month, day, hour, build_date, build_time);

  /////////////////////////////////////////////////////////////
  // the manager doesn't like empty strings, so prevent them //
  /////////////////////////////////////////////////////////////

  if (AppStr[0] == '\0')
    ink_strncpy(AppStr, "?", sizeof(AppStr));
  if (VersionStr[0] == '\0')
    ink_strncpy(VersionStr, "?", sizeof(VersionStr));
  if (BldNumStr[0] == '\0')
    ink_strncpy(BldNumStr, "?", sizeof(BldNumStr));
  if (BldTimeStr[0] == '\0')
    ink_strncpy(BldTimeStr, "?", sizeof(BldTimeStr));
  if (BldDateStr[0] == '\0')
    ink_strncpy(BldDateStr, "?", sizeof(BldDateStr));
  if (BldMachineStr[0] == '\0')
    ink_strncpy(BldMachineStr, "?", sizeof(BldMachineStr));
  if (BldPersonStr[0] == '\0')
    ink_strncpy(BldPersonStr, "?", sizeof(BldPersonStr));
  if (BldCompileFlagsStr[0] == '\0')
    ink_strncpy(BldCompileFlagsStr, "?", sizeof(BldCompileFlagsStr));
  if (FullVersionInfoStr[0] == '\0')
    ink_strncpy(FullVersionInfoStr, "?", sizeof(FullVersionInfoStr));

  defined = 1;
}
