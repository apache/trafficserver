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
#include "ts/I_Version.h"
#include "ts/ink_string.h"

AppVersionInfo::AppVersionInfo()
{
  defined = 0;
  ink_strlcpy(PkgStr, "?", sizeof(PkgStr));
  ink_strlcpy(AppStr, "?", sizeof(AppStr));
  ink_strlcpy(VersionStr, "?", sizeof(VersionStr));
  ink_strlcpy(BldNumStr, "?", sizeof(BldNumStr));
  ink_strlcpy(BldTimeStr, "?", sizeof(BldTimeStr));
  ink_strlcpy(BldDateStr, "?", sizeof(BldDateStr));
  ink_strlcpy(BldMachineStr, "?", sizeof(BldMachineStr));
  ink_strlcpy(BldPersonStr, "?", sizeof(BldPersonStr));
  ink_strlcpy(BldCompileFlagsStr, "?", sizeof(BldCompileFlagsStr));
  ink_strlcpy(FullVersionInfoStr, "?", sizeof(FullVersionInfoStr));
  // coverity[uninit_member]
}

void
AppVersionInfo::setup(const char *pkg_name, const char *app_name, const char *app_version, const char *build_date,
                      const char *build_time, const char *build_machine, const char *build_person, const char *build_cflags)
{
  char month_name[8];
  int year, month, day, hour, minute, second;
  bool invalid_datetime;

  static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "???"};

  invalid_datetime = sscanf(build_time, "%d:%d:%d", &hour, &minute, &second) < 3;
  invalid_datetime |= sscanf(build_date, "%3s %d %d", month_name, &day, &year) < 3;

  // Jan=1, Feb=2 ... Dec=12, ???=13
  for (month = 0; month < 11; month++) {
    if (strcasecmp(months[month], month_name) == 0) {
      break;
    }
  }
  month++;

  ///////////////////////////////////////////
  // now construct the version information //
  ///////////////////////////////////////////
  ink_strlcpy(PkgStr, pkg_name, sizeof(PkgStr));
  ink_strlcpy(AppStr, app_name, sizeof(AppStr));
  snprintf(VersionStr, sizeof(VersionStr), "%s", app_version);

  // If the builder set a build number, use that.
  // Otherwise take the build timestamp ("??????" if invalid).
  if (0 != strlen(BUILD_NUMBER)) {
    snprintf(BldNumStr, sizeof(BldNumStr), "%s", BUILD_NUMBER);
  } else if (!invalid_datetime) {
    snprintf(BldNumStr, sizeof(BldNumStr), "%02d%02d%02d", month, day, hour);
  } else {
    snprintf(BldNumStr, sizeof(BldNumStr), "??????");
  }

  snprintf(BldTimeStr, sizeof(BldTimeStr), "%s", build_time);
  snprintf(BldDateStr, sizeof(BldDateStr), "%s", build_date);
  snprintf(BldMachineStr, sizeof(BldMachineStr), "%s", build_machine);
  snprintf(BldPersonStr, sizeof(BldPersonStr), "%s", build_person);
  snprintf(BldCompileFlagsStr, sizeof(BldCompileFlagsStr), "%s", build_cflags);

  /////////////////////////////////////////////////////////////
  // the manager doesn't like empty strings, so prevent them //
  /////////////////////////////////////////////////////////////
  if (PkgStr[0] == '\0') {
    ink_strlcpy(PkgStr, "?", sizeof(PkgStr));
  }
  if (AppStr[0] == '\0') {
    ink_strlcpy(AppStr, "?", sizeof(AppStr));
  }
  if (VersionStr[0] == '\0') {
    ink_strlcpy(VersionStr, "?", sizeof(VersionStr));
  }
  if (BldNumStr[0] == '\0') {
    ink_strlcpy(BldNumStr, "?", sizeof(BldNumStr));
  }
  if (BldTimeStr[0] == '\0') {
    ink_strlcpy(BldTimeStr, "?", sizeof(BldTimeStr));
  }
  if (BldDateStr[0] == '\0') {
    ink_strlcpy(BldDateStr, "?", sizeof(BldDateStr));
  }
  if (BldMachineStr[0] == '\0') {
    ink_strlcpy(BldMachineStr, "?", sizeof(BldMachineStr));
  }
  if (BldPersonStr[0] == '\0') {
    ink_strlcpy(BldPersonStr, "?", sizeof(BldPersonStr));
  }
  if (BldCompileFlagsStr[0] == '\0') {
    ink_strlcpy(BldCompileFlagsStr, "?", sizeof(BldCompileFlagsStr));
  }
  if (FullVersionInfoStr[0] == '\0') {
    ink_strlcpy(FullVersionInfoStr, "?", sizeof(FullVersionInfoStr));
  }

  snprintf(FullVersionInfoStr, sizeof(FullVersionInfoStr), "%s - %s - %s - (build # %s on %s at %s)", PkgStr, AppStr, VersionStr,
           BldNumStr, BldDateStr, BldTimeStr);

  defined = 1;
}

#if TS_HAS_TESTS
#include <ts/TestBox.h>

/**
 * AppVersionInfo class test.
 */
REGRESSION_TEST(AppVersionInfo)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  *pstatus = REGRESSION_TEST_PASSED;

  AppVersionInfo info;

  TestBox tb(t, pstatus);

  const char *errMsgFormat = "wrong build number, expected '%s', got '%s'";
  const char *bench[][3]   = {// date, time, resulting build number
                            {"Oct  4 1957", "19:28:34", BUILD_NUMBER},
                            {"Oct  4 1957", "19:28:34", "100419"},
                            {"Apr  4 1957", "09:08:04", "040409"},
                            {" 4 Apr 1957", "09:08:04", "??????"},
                            {"Apr  4 1957", "09-08-04", "??????"}};

  int benchSize = sizeof(bench) / sizeof(bench[0]);

  if (0 != strlen(BUILD_NUMBER)) {
    // Since BUILD_NUMBER is defined by a #define directive, it is not
    // possible to change the version value from inside the regression test.
    // If not empty BUILD_NUMBER overrides any result, in this case run only
    // this test (the rest will always fail).
    info.setup("Apache Traffic Server", "traffic_server", "5.2.1", bench[0][0], bench[0][1], "build_slave", "builder", "");
    tb.check(0 == strcmp(info.BldNumStr, bench[0][2]), errMsgFormat, bench[0][2], info.BldNumStr);
  } else {
    for (int i = 1; i < benchSize; i++) {
      info.setup("Apache Traffic Server", "traffic_server", "5.2.1", bench[i][0], bench[i][1], "build_slave", "builder", "");
      tb.check(0 == strcmp(info.BldNumStr, bench[i][2]), errMsgFormat, bench[i][2], info.BldNumStr);
    }
  }
}
#endif
