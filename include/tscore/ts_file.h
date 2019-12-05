/** @file

  Simple path and file utilities.

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

#include <string>
#include <string_view>
#include <array>
#include <system_error>
#include <sys/stat.h>
#include "tscore/ink_memory.h"
#include "tscpp/util/TextView.h"
#include "tscore/BufferWriter.h"

namespace ts
{
namespace file
{
  /** Utility class for file system paths.
   */
  class path
  {
    using self_type = path;

  public:
    using value_type                          = char;
    using string_type                         = std::string;
    static constexpr char preferred_separator = value_type{'/'};

    /// Default construct empty path.
    path() = default;

    /// Copy constructor - copies the path.
    path(const self_type &that) = default;

    /// Move constructor.
    path(self_type &&that) = default;

    /// Construct from a null terminated string.
    explicit path(const char *src);

    /// Construct from a string view.
    path(std::string_view src);
    //  template < typename ... Args > explicit path(std::string_view base, Args... rest);

    /// Move from an existing string
    path(std::string &&that);

    /// Replace the path with a copy of @a that.
    self_type &operator=(const self_type &that) = default;

    /// Replace the path with the contents of @a that.
    self_type &operator=(self_type &&that) = default;

    /// Assign @a p as the path.
    self_type &operator=(std::string_view p);

    /** Append or replace path with @a that.
     *
     * If @a that is absolute, it replaces @a this. Otherwise @a that is appended with exactly one
     * separator.
     *
     * @param that Filesystem path.
     * @return @a this
     */
    self_type &operator/=(const self_type &that);
    self_type &operator/=(std::string_view that);

    /// Check if the path is empty.
    bool empty() const;

    /// Check if the path is absolute.
    bool is_absolute() const;

    /// Check if the path is not absolute.
    bool is_relative() const;

    /// Access the path explicitly.
    char const *c_str() const;

    /// Get a view of the path.
    std::string_view view() const;

    /// Get a copy of the path.
    std::string string() const;

    /// Get relative path
    self_type relative_path();

    /// Get parent path
    path parent_path();

  protected:
    std::string _path; ///< File path.
  };

  /// Information about a file.
  class file_status
  {
    using self_type = file_status;

  public:
  protected:
    struct ::stat _stat; ///< File information.

    friend self_type status(const path &, std::error_code &) noexcept;

    friend int file_type(const self_type &);
    friend time_t modification_time(const file_status &fs);
    friend uintmax_t file_size(const self_type &);
    friend bool is_regular_file(const file_status &);
    friend bool is_dir(const file_status &);
    friend bool is_char_device(const file_status &);
    friend bool is_block_device(const file_status &);
  };

  /** Get the status of the file at @a p.
   *
   * @param p Path to file.
   * @param ec Error code return.
   * @return Status of the file.
   */
  file_status status(const path &p, std::error_code &ec) noexcept;

  // Related free functions.
  // These are separate because they are not part of std::filesystem::path.

  /// Return the file type value.
  int file_type(const file_status &fs);

  /// Return modification time
  time_t modification_time(const file_status &fs);

  /// Check if the path is to a regular file.
  bool is_regular_file(const file_status &fs);

  /// Check if the path is to a directory.
  bool is_dir(const file_status &p);

  /// Check if the path is to a character device.
  bool is_char_device(const file_status &fs);

  /// Check if the path is to a block device.
  bool is_block_device(const file_status &fs);

  /// Size of the file or block device.
  uintmax_t file_size(const file_status &fs);

  /// Check if file is readable.
  bool is_readable(const path &s);

  // Get directory location suitable for temporary files
  path temp_directory_path();

  // Returns current path.
  path current_path();

  // Returns return the canonicalized absolute pathname
  path canonical(const path &p, std::error_code &ec);

  // Return the filename derived from path p.
  //
  // Examples:
  //   given "/foo/bar.txt", this returns "bar.txt"
  //   given "/foo/bar", this returns "bar"
  //   given "/foo/bar/", this returns ""
  //   given "/", this returns ""
  path filename(const path &p);

  // Checks if the file/directory exists
  bool exists(const path &p);

  // Create directories
  bool create_directories(const path &p, std::error_code &ec, mode_t mode = 0775) noexcept;

  // Copy files ("from" cannot be directory). @todo make it more generic
  bool copy(const path &from, const path &to, std::error_code &ec);

  // Removes files and directories recursively
  bool remove(const path &path, std::error_code &ec);

  /** Load the file at @a p into a @c std::string
   *
   * @param p Path to file
   * @return The contents of the file.
   */
  std::string load(const path &p, std::error_code &ec);
  /* ------------------------------------------------------------------- */

  inline path::path(char const *src) : _path(src) {}

  inline path::path(std::string_view base) : _path(base) {}

  inline path::path(std::string &&that) : _path(std::move(that)) {}

  inline path &
  path::operator=(std::string_view p)
  {
    _path.assign(p);
    return *this;
  }

  inline char const *
  path::c_str() const
  {
    return _path.c_str();
  }

  inline std::string_view
  path::view() const
  {
    return _path;
  }

  inline std::string
  path::string() const
  {
    return _path;
  }

  inline bool
  path::empty() const
  {
    return _path.empty();
  }

  inline bool
  path::is_absolute() const
  {
    return !_path.empty() && preferred_separator == _path[0];
  }

  inline bool
  path::is_relative() const
  {
    return !this->is_absolute();
  }

  inline path &
  path::operator/=(const self_type &that)
  {
    return *this /= std::string_view(that._path);
  }

  inline path
  path::relative_path()
  {
    return (this->is_absolute()) ? path(_path.substr(sizeof(preferred_separator))) : *this;
  }

  inline path
  path::parent_path()
  {
    if (this->is_absolute() && _path.substr(sizeof(preferred_separator)).empty()) {
      // No relative path
      return *this;
    }

    const size_t last_slash_idx = _path.find_last_of(preferred_separator);
    if (std::string::npos != last_slash_idx) {
      return path(_path.substr(0, last_slash_idx));
    } else {
      return path();
    }
  }

  /** Combine two strings as file paths.

       @return A @c path with the combined path.
  */
  inline path
  operator/(const path &lhs, const path &rhs)
  {
    return path(lhs) /= rhs;
  }

  inline path
  operator/(path &&lhs, const path &rhs)
  {
    return path(std::move(lhs)) /= rhs;
  }

  inline path
  operator/(const path &lhs, std::string_view rhs)
  {
    return path(lhs) /= rhs;
  }

  inline path
  operator/(path &&lhs, std::string_view rhs)
  {
    return path(std::move(lhs)) /= rhs;
  }

  inline bool
  operator==(const path &lhs, const path &rhs)
  {
    return lhs.string() == rhs.string();
  }

  inline bool
  operator!=(const path &lhs, const path &rhs)
  {
    return lhs.string() != rhs.string();
  }

  /* ------------------------------------------------------------------- */
} // namespace file
} // namespace ts
/* ------------------------------------------------------------------- */
