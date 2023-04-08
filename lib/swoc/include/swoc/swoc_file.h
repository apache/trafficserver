// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

  Simple path and file utilities.
 */

#pragma once

#include <unistd.h>
#include <sys/stat.h>

#include <string>
#include <string_view>
#include <system_error>
#include <chrono>

#include "swoc/swoc_version.h"
#include "swoc/TextView.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
namespace file {

using file_time_type = std::chrono::system_clock::time_point;

enum class file_type : signed char {
  none = 0, not_found = -1, regular = 1, directory = 2, symlink = 3,
  block = 4, character = 5, fifo = 6, socket = 7, unknown = 8
};

/// Invalid file descriptor.
static constexpr int NO_FD = -1;

/// Scoped container for a file descriptor.
struct unique_fd {
  using self_type = unique_fd; ///< Self reference type.

  /** Construct with file descriptor.
   *
   * @param fd File descriptor.
   *
   * The instance takes owernship of the file descriptor.
   */
  explicit unique_fd(int fd) : _fd(fd) {}

  /// Non-copyable.
  unique_fd(self_type const &) = delete;

  /** Move construction.
   *
   * @param that Source instance.
   *
   * Ownership is from from @Wa that to @a this.
   */
  unique_fd(self_type && that) : _fd(that._fd) { that._fd = NO_FD; }

  /// Close the file dscriptor.
  ~unique_fd() { if (_fd != NO_FD) { ::close(_fd); _fd = NO_FD; }; }

  /** Release ownership of the file descriptor.
   *
   * @return The file descriptor.
   */
  int release() { auto zret = _fd; _fd = NO_FD; return zret; }

  /// Implicitly convert to file descriptor.
  operator int() const { return _fd; }

protected:
  int _fd = NO_FD;
};

/** Utility class for file system paths.
 */
class path {
  using self_type = path; ///< Self reference type.

public:
  /// Default path separator.
  static constexpr char SEPARATOR = '/';

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

  /** Append or replace path with @a that.
   *
   * If @a that is absolute, it replaces @a this. Otherwise @a that is appended with exactly one
   * separator.
   *
   * @param that Filesystem path.
   * @return @a this
   */
  self_type &operator/=(std::string_view that);

  /// Check if the path is empty.
  bool empty() const;

  /// Check if the path is absolute.
  bool is_absolute() const;

  /// Check if the path is not absolute.
  bool is_relative() const;

  /// Path of the parent.
  self_type parent_path() const;

  /// @return Path excluding the root path, if nay.
  self_type relative_path() const;

  /** Filename part of the path.
   *
   * @param p Path.
   * @return
   */
  self_type filename() const;

  /// Access the path explicitly.
  char const *c_str() const;

  /// The path as a string.
  std::string const &string() const;

  /** Reserve space in the path.
   *
   * @param n Number of bytes to reserve.
   * @return @a this.
   */
  self_type & reserve(size_t n);

  /// A view of the path.
  swoc::TextView view() const;

protected:
  std::string _path; ///< File path.
};

/// Information about a file.
class file_status {
  using self_type = file_status;

public:
  /// @return File type as an enumeration.
  file_type type() const;

  /// @return raw file mode data.
  mode_t mode() const;

protected:
  struct ::stat _stat; ///< File information.
  file_type _type = file_type::none;

  void init();

  friend self_type status(const path &file, std::error_code &ec) noexcept;
  friend int file_type(const self_type &);
  friend uintmax_t file_size(const self_type &);
  friend bool is_regular_file(const self_type &);
  friend bool is_dir(const self_type &);
  friend bool is_char_device(const self_type &);
  friend bool is_block_device(const self_type &);
  friend bool exists(const self_type &);
  friend file_time_type last_write_time(file_status const &fs);
  friend file_time_type access_time(file_status const &fs);
  friend file_time_type status_time(file_status const &fs);
};

/** Get the status of the file at @a p.
 *
 * @param file Path to file.
 * @param ec Error code return.
 * @return Status of the file.
 */
file_status status(const path &file, std::error_code &ec) noexcept;

// Related free functions.
// These are separate because they are not part of std::filesystem::path.

/// Return the file type value.
[[deprecated("Use file_status::type")]] int file_type(const file_status &fs);

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

/// Check if path exists.
bool exists(const path &p);

/// Check if path exists.
bool exists(const file_status &fs);

/** Convert to absolute path.
 *
 * @param src Original path
 * @param ec Error code.
 * @return Absolute path.
 *
 * If @a path is already absolute, a copy of it is returned. Otherwise an absolute path is
 * constructed that refers to the same item in the file system as @a src. If an error occurs
 * then @a ec is set to indicate the type of error.
 */
path absolute(path const &src, std::error_code &ec);

/// Directory location suitable for temporary files
path temp_directory_path();

/// Current working directory.
path current_path();

/// @return Canonicalized absolute pathname
path canonical(const path &p, std::error_code &ec);

/** Create directory.
 *
 * @param p Path to directory.
 * @param ec Error code return.
 * @param mode Permissions for created directory.
 * @return @c true if @a p was created, @c false otherwise.
 *
 * @note Not fully implemented.
 */
bool create_directory(const path &p, std::error_code &ec, mode_t mode = 0775) noexcept;

/** Create directories.
 *
 * @param p Path to directory.
 * @param ec Error code return.
 * @param mode Permissions for created directories.
 * @return @c true if @a p was created, @c false otherwise.
 *
 * @note Not fully implemented.
 */
bool create_directories(const path &p, std::error_code &ec, mode_t mode = 0775) noexcept;

/** Copy files.
 *
 * @param from Source file
 * @param to Destination file.
 * @param ec Error code return.
 * @return @c true if @a from was copied, @c false on error.
*
* @note Not fully implemented.
 */
bool copy(const path &from, const path &to, std::error_code &ec);

/** Remove a file or empty directory.
 *
 * @param path Target.
 * @param ec Error return.
 * @return @c true if the operation succeeded, @c false if @a ec has an error code.
 */
bool remove(const path &path, std::error_code &ec);

/** Remove a file or a directory and all nested files.
 *
 * @param path Target.
 * @param ec Error return.
 * @return Number of items removed.
 */
uintmax_t remove_all(const path &path, std::error_code &ec);

/// @return The modified time for @a fs.
[[deprecated("See last_write_time")]] file_time_type modify_time(file_status const &fs);
/// @return The modified time for @a fs.
file_time_type last_write_time(file_status const &fs);

/** Modification time.
 *
 * @param p Path to target.
 * @return Time of last modification, or @c file_time_type::min() on error.
 */
file_time_type last_write_time(path const& p, std::error_code &ec);

/// @return The access time for @a fs.
file_time_type access_time(file_status const &fs);
/// @return The status change time for @a fs.
file_time_type status_time(file_status const &fs);

/** Load the file at @a p into a @c std::string.
 *
 * @param p Path to file
 * @param ec Error code result of the file operation.
 * @return The contents of the file.
 */
std::string load(const path &p, std::error_code &ec);

/* ------------------------------------------------------------------- */

inline path::path(char const *src) : _path(src) {}

inline path::path(std::string_view base) : _path(base) {}

inline path::path(std::string &&that) : _path(std::move(that)) {}

inline path &
path::operator=(std::string_view p) {
  _path.assign(p);
  return *this;
}

inline char const *
path::c_str() const {
  return _path.c_str();
}

inline std::string const &
path::string() const {
  return _path;
}

inline swoc::TextView
path::view() const {
  return {_path};
}

inline bool
path::empty() const {
  return _path.empty();
}

inline bool
path::is_absolute() const {
  return !_path.empty() && '/' == _path[0];
}

inline bool
path::is_relative() const {
  return !this->is_absolute();
}

inline path &
path::operator/=(const self_type &that) {
  return *this /= std::string_view(that._path);
}

/** Compare two paths.
 *
 * @return @c true if @a lhs is identical to @a rhs.
 */
inline bool
operator==(path const &lhs, path const &rhs) {
  return lhs.view() == rhs.view();
}

/** Compare two paths.
 *
 * @return @c true if @a lhs is not identical to @a rhs.
 */
inline bool
operator!=(path const &lhs, path const &rhs) {
  return lhs.view() != rhs.view();
}

/** Combine two strings as file paths.
 *
 * @return A @c path with the combined path.
 */
inline path
operator/(const path &lhs, const path &rhs) {
  return path(lhs) /= rhs;
}

/** Combine two strings as file paths.
 *
 * @return A @c path with the combined path.
 */
inline path
operator/(path &&lhs, const path &rhs) {
  return path(std::move(lhs)) /= rhs;
}

/** Combine two strings as file paths.
 *
 * @return A @c path with the combined path.
 */
inline path
operator/(const path &lhs, std::string_view rhs) {
  return path(lhs) /= rhs;
}

inline auto
path::reserve(size_t n) -> self_type & {
  _path.reserve(n);
  return *this;
}

/** Combine two strings as file paths.
 *
 * @return A @c path with the combined path.
 */
inline path
operator/(path &&lhs, std::string_view rhs) {
  return path(std::move(lhs)) /= rhs;
}

// Can remove "enum" after the function @c file_type is removed.
inline enum file_type file_status::type() const { return _type; }

inline mode_t file_status::mode() const { return _stat.st_mode; }

inline bool
is_char_device(const file_status &fs) {
  return fs._type == file_type::character;
}

inline bool
is_block_device(const file_status &fs) {
  return fs._type == file_type::block;
}

inline bool
is_regular_file(const file_status &fs) {
  return fs._type == file_type::regular;
}

inline bool
is_dir(const file_status &fs) {
  return fs._type == file_type::directory;
}

inline bool
exists(const file_status &fs) {
  return fs._type != file_type::none && fs._type != file_type::not_found;
}

inline file_time_type
modify_time(file_status const &fs) {
  return last_write_time(fs);
}
} // namespace file

class BufferWriter;
namespace bwf {
struct Spec;
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, file::path const &p);
}} // namespace swoc::SWOC_VERSION_NS

namespace std {
/// Enable use of path as a key in STL hashed containers.
template <> struct hash<swoc::file::path> {
  size_t
  operator()(swoc::file::path const &path) const {
    return hash<string_view>()(path.view());
  }
};
} // namespace std
