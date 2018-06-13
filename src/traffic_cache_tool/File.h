/** @file

  File system support classes.

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

#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "ts/ink_memory.h"
#include "ts/TextView.h"

namespace ts
{
/** A file class for supporting path operations.
 */
class FilePath
{
  typedef FilePath self; ///< Self reference type.
public:
  FilePath();
  /// Construct from a null terminated string.
  explicit FilePath(char const *path);
  /// Construct from a string view.
  explicit FilePath(TextView const &path);
  /// Copy constructor - copies the path.
  FilePath(self const &that);
  /// Move constructor.
  FilePath(self &&that);
  /// Assign a new path.
  self &operator=(char const *path);
  /// Combine two paths, making sure there is exactly one separator between them.
  self operator/(self const &rhs);
  /// Create a new instance by appended @a path.
  self operator/(char const *path);
  /// Check if there is a path.
  bool has_path() const;
  /// Check if the path is absolute.
  bool is_absolute() const;
  /// Check if the path is not absolute.
  bool is_relative() const;
  /// Check if file is readable.
  bool is_readable() const;
  /// Access the path as a null terminated string.
  operator const char *() const;
  /// Access the path explicitly.
  char const *path() const;

  /// Return the file type value.
  int file_type() const;
  /// Size of the file or block device.
  off_t physical_size() const;

  bool is_char_device() const;
  bool is_block_device() const;
  bool is_dir() const;
  bool is_regular_file() const;

  // Utility methods.
  ats_scoped_fd open(int flags) const;

protected:
  /// Get the stat buffer.
  /// @return A valid stat buffer or @c nullptr if the system call failed.
  template <typename T> T stat(T (*f)(struct stat const *)) const;

  std::string _path; ///< File path.

  enum class STAT_P : int8_t { INVALID = -1, UNDEF = 0, VALID = 1 };
  mutable STAT_P _stat_p = STAT_P::UNDEF; ///< Whether _stat is valid.
  mutable struct stat _stat;              ///< File information.
};

/** A file support class for handling files as bulk content.

    @note This is used primarily for configuration files where the entire file is read every time
    and it's rarely (if ever) useful to read it incrementally. The general scheme is the entire file
    is read and then @c TextView elements are used to reference the bulk content.

    @internal The design goal of this class is to supplant the free functions later in this header.

 */
class BulkFile : public FilePath
{
  typedef BulkFile self;  ///< Self reference type.
  typedef FilePath super; ///< Parent type.
public:
  // Inherit super class constructors.
  using super::super;
  ///< Conversion constructor from base class.
  BulkFile(super &&that);
  /// Read the contents of the file in a local buffer.
  /// @return @c errno
  int load();
  TextView content() const;

private:
  std::string _content; ///< The file contents.
  size_t _len = -1;     ///< Length of file content.
};

/* ------------------------------------------------------------------- */

inline FilePath::FilePath()
{
  ink_zero(_stat);
}
inline FilePath::FilePath(char const *path) : _path(path)
{
  ink_zero(_stat);
}
inline FilePath::FilePath(TextView const &path) : _path(path.data(), path.size())
{
  ink_zero(_stat);
}
inline FilePath::FilePath(self const &that) : _path(that._path)
{
  ink_zero(_stat);
}
inline FilePath::FilePath(self &&that) : _path(std::move(that._path))
{
  ink_zero(_stat);
}
inline FilePath::operator const char *() const
{
  return _path.c_str();
}
inline char const *
FilePath::path() const
{
  return _path.c_str();
}

inline bool
FilePath::has_path() const
{
  return !_path.empty();
}
inline bool
FilePath::is_absolute() const
{
  return !_path.empty() && '/' == _path[0];
}
inline bool
FilePath::is_relative() const
{
  return !this->is_absolute();
}

template <typename T> T FilePath::stat(T (*f)(struct stat const *)) const
{
  if (STAT_P::UNDEF == _stat_p) {
    _stat_p = ::stat(_path.c_str(), &_stat) >= 0 ? STAT_P::VALID : STAT_P::INVALID;
  }
  return _stat_p == STAT_P::VALID ? f(&_stat) : T();
}

FilePath operator/(FilePath const &lhs, FilePath const &rhs);
FilePath operator/(char const *lhs, FilePath const &rhs);

inline int
FilePath::file_type() const
{
  return this->stat<int>([](struct stat const *s) -> int { return s->st_mode & S_IFMT; });
}

inline bool
FilePath::is_dir() const
{
  return this->file_type() == S_IFDIR;
}
inline bool
FilePath::is_char_device() const
{
  return this->file_type() == S_IFCHR;
}
inline bool
FilePath::is_block_device() const
{
  return this->file_type() == S_IFBLK;
}
inline bool
FilePath::is_regular_file() const
{
  return this->file_type() == S_IFREG;
}

inline off_t
FilePath::physical_size() const
{
  return this->stat<off_t>([](struct stat const *s) { return s->st_size; });
}

inline BulkFile::BulkFile(super &&that) : super(that) {}

/* ------------------------------------------------------------------- */
} // namespace ts
/* ------------------------------------------------------------------- */
