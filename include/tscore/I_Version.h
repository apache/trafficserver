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

#pragma once

namespace ts
{
/** Container for standard two part version number.
 */
struct VersionNumber {
  /// Construct invalid (0.0) version.
  constexpr VersionNumber() = default;

  /// Construct explicit version.
  constexpr explicit VersionNumber(unsigned short major, unsigned short minor = 0);

  // Can't use unadorned "major" because that's a macro.
  unsigned short _major = 0; ///< Major version.
  unsigned short _minor = 0; ///< Minor version.
};

inline constexpr VersionNumber::VersionNumber(unsigned short major, unsigned short minor) : _major(major), _minor(minor) {}

inline bool
operator<(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return lhs._major < rhs._major || (lhs._major == rhs._major && lhs._minor < rhs._minor);
}

inline bool
operator==(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return lhs._major == rhs._major && lhs._minor == rhs._minor;
}

inline bool
operator!=(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return !(lhs == rhs);
}

inline bool
operator>(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return rhs < lhs;
}

inline bool
operator<=(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return !(lhs > rhs);
}

inline bool
operator>=(VersionNumber const &lhs, VersionNumber const &rhs)
{
  return !(rhs > lhs);
}

struct Version {
  VersionNumber cacheDB;
  VersionNumber cacheDir;
};

/// Container for a module version.
struct ModuleVersion {
  /// Type of module.
  enum Type : unsigned char { PUBLIC, PRIVATE };

  constexpr ModuleVersion(unsigned char major, unsigned char minor, Type type = PUBLIC);
  constexpr ModuleVersion(ModuleVersion const &base, Type type);

  /** Check if @a that is a version compatible with @a this.
   *
   * @param that Version to check against.
   * @return @a true if @a that is compatible with @a this, @c false otherwise.
   */
  bool check(ModuleVersion const &that);

  Type _type           = PUBLIC; ///< The numeric value of the module version.
  unsigned char _major = 0;      ///< Major version.
  unsigned char _minor = 0;
};

inline constexpr ModuleVersion::ModuleVersion(unsigned char major, unsigned char minor, ts::ModuleVersion::Type type)
  : _type(type), _major(major), _minor(minor)
{
}

inline constexpr ModuleVersion::ModuleVersion(ModuleVersion const &base, ts::ModuleVersion::Type type)
  : _type(type), _major(base._major), _minor(base._minor)
{
}

inline bool
ModuleVersion::check(ModuleVersion const &that)
{
  switch (_type) {
  case PUBLIC:
    return _major == that._major && _minor <= that._minor;
  case PRIVATE:
    return _major == that._major && _minor == that._minor;
  }
  return false;
};

} // namespace ts

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
