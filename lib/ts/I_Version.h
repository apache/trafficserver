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

/**************************************************************************

  Version.h


  **************************************************************************/

#ifndef _Version_h
#define _Version_h

struct VersionNumber {
  short int ink_major; // incompatible change
  short int ink_minor; // minor change, not incompatible

  VersionNumber() {}
  VersionNumber(short int major, short int minor) : ink_major(major), ink_minor(minor) {}
};

inline bool
operator<(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return lhs.ink_major < rhs.ink_major || (lhs.ink_major == rhs.ink_major && lhs.ink_minor < rhs.ink_minor);
}

struct Version {
  VersionNumber cacheDB;
  VersionNumber cacheDir;
  VersionNumber clustering;
  VersionNumber clustering_min;
};

enum ModuleVersion {
  MODULE_VERSION_MIN = 0,
  MODULE_VERSION_MAX = 2147483647,
};
enum ModuleHeaderType {
  PUBLIC_MODULE_HEADER,
  PRIVATE_MODULE_HEADER,
};

#define makeModuleVersion(_major_version, _minor_version, _module_type) \
  ((ModuleVersion)((((int)_module_type) << 24) + (((int)_major_version) << 16) + (((int)_minor_version) << 8)))

#define majorModuleVersion(_v) ((((int)_v) >> 16) & 255)
#define minorModuleVersion(_v) ((((int)_v) >> 8) & 255)
#define moduleVersionType(_v) ((((int)_v) >> 24) & 127)

static inline int
checkModuleVersion(ModuleVersion userVersion, ModuleVersion libVersion)
{
  if (moduleVersionType(userVersion) == PUBLIC_MODULE_HEADER) {
    if ((majorModuleVersion(userVersion) != majorModuleVersion(libVersion)) ||
        (minorModuleVersion(userVersion) > minorModuleVersion(libVersion)))
      return -1;
    return 0;
  } else if (moduleVersionType(userVersion) == PRIVATE_MODULE_HEADER) {
    if ((majorModuleVersion(userVersion) != majorModuleVersion(libVersion)) ||
        (minorModuleVersion(userVersion) != minorModuleVersion(libVersion)))
      return -1;
    return 0;
  } else
    return -1;
}

class AppVersionInfo
{
public:
  int defined;
  char PkgStr[128];
  char AppStr[128];
  char VersionStr[128];
  char BldNumStr[128];
  char BldTimeStr[128];
  char BldDateStr[128];
  char BldMachineStr[128];
  char BldPersonStr[128];
  char BldCompileFlagsStr[128];
  char FullVersionInfoStr[256];

  AppVersionInfo();
  void setup(const char *pkg_name, const char *app_name, const char *app_version, const char *build_date, const char *build_time,
             const char *build_machine, const char *build_person, const char *build_cflags);
};

#endif /*_Version_h*/
