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

#if !defined(ATS_FILE_HEADER)
#define ATS_FILE_HEADER

#include <ts/ink_memory.h>
#include <sys/stat.h>
#include <ts/MemView.h>

namespace ApacheTrafficServer
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
  explicit FilePath(StringView const &path);
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

  /// Get the stat buffer.
  /// @return A valid stat buffer or @c nullptr if the system call failed.
  struct stat const *stat() const;

  /// Return the file type value.
  int file_type() const;

  bool is_char_device() const;
  bool is_block_device() const;
  bool is_dir() const;
  bool is_regular_file() const;

  // Utility methods.
  ats_scoped_fd open(int flags) const;

protected:
  ats_scoped_str _path;         ///< File path.
  mutable struct stat _stat;    ///< File information.
  mutable bool _stat_p = false; ///< Whether _stat is valid.
};

/** A file support class for handling files as bulk content.

    @note This is used primarily for configuration files where the entire file is read every time
    and it's rarely (if ever) useful to read it incrementally. The general scheme is the entire file
    is read and then @c StringView elements are used to reference the bulk content.

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
  StringView content() const;

private:
  ats_scoped_str _content; ///< The file contents.
  size_t _len;             ///< Length of file content.
};

/* ------------------------------------------------------------------- */

inline FilePath::FilePath()
{
}
inline FilePath::FilePath(char const *path) : _path(ats_strdup(path))
{
}
inline FilePath::FilePath(StringView const &path)
{
  _path = static_cast<char *>(ats_malloc(path.size() + 1));
  memcpy(_path, path.ptr(), path.size());
  _path[path.size()] = 0;
}
inline FilePath::FilePath(self const &that) : _path(ats_strdup(static_cast<char const *>(that)))
{
}
inline FilePath::FilePath(self &&that) : _path(static_cast<ats_scoped_str &&>(that._path))
{
}
inline FilePath::operator const char *() const
{
  return _path;
}
inline char const *
FilePath::path() const
{
  return _path;
}

inline bool
FilePath::has_path() const
{
  return _path && 0 != _path[0];
}
inline bool
FilePath::is_absolute() const
{
  return _path && '/' == _path[0];
}
inline bool
FilePath::is_relative() const
{
  return !this->is_absolute();
}

inline struct stat const *
FilePath::stat() const
{
  if (!_stat_p)
    _stat_p = ::stat(_path, &_stat) >= 0;
  return _stat_p ? &_stat : nullptr;
}

FilePath operator/(FilePath const &lhs, FilePath const &rhs);
FilePath operator/(char const *lhs, FilePath const &rhs);

inline int
FilePath::file_type() const
{
  return this->stat() ? (_stat.st_mode & S_IFMT) : 0;
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

inline BulkFile::BulkFile(super &&that) : super(that)
{
}

/* ------------------------------------------------------------------- */
} // namespace
/* ------------------------------------------------------------------- */

#endif
