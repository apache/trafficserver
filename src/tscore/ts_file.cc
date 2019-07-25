/** @file

    Minimalist version of std::filesystem.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#include "tscore/ts_file.h"
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

namespace ts
{
namespace file
{
  path &
  path::operator/=(std::string_view that)
  {
    if (!that.empty()) { // don't waste time appending nothing.
      if (that.front() == preferred_separator || _path.empty()) {
        _path.assign(that);
      } else {
        if (_path.back() == preferred_separator) {
          _path.reserve(_path.size() + that.size());
        } else {
          _path.reserve(_path.size() + that.size() + 1);
          _path.push_back(preferred_separator);
        }
        _path.append(that);
      }
    }
    return *this;
  }

  file_status
  status(const path &p, std::error_code &ec) noexcept
  {
    file_status zret;
    if (::stat(p.c_str(), &zret._stat) >= 0) {
      ec.clear();
    } else {
      ec = std::error_code(errno, std::system_category());
    }
    return zret;
  }

  path
  temp_directory_path()
  {
    /* ISO/IEC 9945 (POSIX): The path supplied by the first environment variable found in the list TMPDIR, TMP, TEMP, TEMPDIR.
     * If none of these are found, "/tmp" */
    char const *folder = nullptr;
    if ((nullptr == (folder = getenv("TMPDIR"))) && (nullptr == (folder = getenv("TMP"))) &&
        (nullptr == (folder = getenv("TEMPDIR")))) {
      folder = "/tmp";
    }
    return path(folder);
  }

  path
  current_path()
  {
    char cwd[PATH_MAX];
    if (::getcwd(cwd, sizeof(cwd)) != NULL) {
      return path(cwd);
    }
    return path();
  }

  path
  canonical(const path &p, std::error_code &ec)
  {
    if (p.empty()) {
      ec = std::error_code(EINVAL, std::system_category());
      return path();
    }

    char buf[PATH_MAX + 1];
    char *res = ::realpath(p.c_str(), buf);
    if (res) {
      ec = std::error_code();
      return path(res);
    }

    ec = std::error_code(errno, std::system_category());
    return path();
  }

  bool
  exists(const path &p)
  {
    std::error_code ec;
    status(p, ec);
    return !(ec && ENOENT == ec.value());
  }

  static bool
  do_mkdir(const path &p, std::error_code &ec, mode_t mode)
  {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) {
      if (mkdir(p.c_str(), mode) != 0 && errno != EEXIST) {
        ec = std::error_code(errno, std::system_category());
        return false;
      }
    } else if (!S_ISDIR(st.st_mode)) {
      ec = std::error_code(ENOTDIR, std::system_category());
      return false;
    }
    return true;
  }

  bool
  create_directories(const path &p, std::error_code &ec, mode_t mode) noexcept
  {
    if (p.empty()) {
      ec = std::error_code(EINVAL, std::system_category());
      return false;
    }

    bool result = false;
    ec          = std::error_code();

    size_t pos = 0;
    std::string token;
    while ((pos = p.string().find_first_of(p.preferred_separator, pos)) != std::string::npos) {
      token = p.string().substr(0, pos);
      if (!token.empty()) {
        result = do_mkdir(path(token), ec, mode);
      }
      pos = pos + sizeof(p.preferred_separator);
    }

    if (result) {
      result = do_mkdir(p, ec, mode);
    }
    return result;
  }

  bool
  copy(const path &from, const path &to, std::error_code &ec)
  {
    static int BUF_SIZE = 65536;
    FILE *src, *dst;
    char buf[BUF_SIZE];
    int bufsize = BUF_SIZE;

    if (from.empty() || to.empty()) {
      ec = std::error_code(EINVAL, std::system_category());
      return false;
    }

    ec = std::error_code();

    std::error_code err;
    path final_to;
    file_status s = status(to, err);
    if (!(err && ENOENT == err.value()) && is_dir(s)) {
      const size_t last_slash_idx = from.string().find_last_of(from.preferred_separator);
      std::string filename        = from.string().substr(last_slash_idx + 1);
      final_to                    = to / filename;
    } else {
      final_to = to;
    }

    if (nullptr == (src = fopen(from.c_str(), "r"))) {
      ec = std::error_code(errno, std::system_category());
      return false;
    }
    if (nullptr == (dst = fopen(final_to.c_str(), "w"))) {
      ec = std::error_code(errno, std::system_category());
      fclose(src);
      return false;
    }

    while (1) {
      size_t in = fread(buf, 1, bufsize, src);
      if (0 == in)
        break;
      size_t out = fwrite(buf, 1, in, dst);
      if (0 == out)
        break;
    }

    fclose(src);
    fclose(dst);

    return true;
  }

  static bool
  remove_path(const path &p, std::error_code &ec)
  {
    DIR *dir;
    struct dirent *entry;
    bool res = true;
    std::error_code err;

    file_status s = status(p, err);
    if (err && ENOENT == err.value()) {
      // file/dir does not exist
      return false;
    } else if (is_regular_file(s)) {
      // regular file, try to remove it!
      if (unlink(p.c_str()) != 0) {
        ec  = std::error_code(errno, std::system_category());
        res = false;
      }
      return res;
    } else if (!is_dir(s)) {
      // not a directory
      ec = std::error_code(ENOTDIR, std::system_category());
      return false;
    }

    // recursively remove nested files and directories
    if (nullptr == (dir = opendir(p.c_str()))) {
      ec = std::error_code(errno, std::system_category());
      return false;
    }

    while (nullptr != (entry = readdir(dir))) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
        continue;
      }

      remove_path(p / entry->d_name, ec);
    }

    if (0 != rmdir(p.c_str())) {
      ec = std::error_code(errno, std::system_category());
    }

    closedir(dir);
    return true;
  }

  bool
  remove(const path &p, std::error_code &ec)
  {
    if (p.empty()) {
      ec = std::error_code(EINVAL, std::system_category());
      return false;
    }

    ec = std::error_code();
    return remove_path(p, ec);
  } // namespace file

  int
  file_type(const file_status &fs)
  {
    return fs._stat.st_mode & S_IFMT;
  }

  time_t
  modification_time(const file_status &fs)
  {
    return fs._stat.st_mtime;
  }
  uintmax_t
  file_size(const file_status &fs)
  {
    return fs._stat.st_size;
  }

  bool
  is_char_device(const file_status &fs)
  {
    return file_type(fs) == S_IFCHR;
  }

  bool
  is_block_device(const file_status &fs)
  {
    return file_type(fs) == S_IFBLK;
  }

  bool
  is_regular_file(const file_status &fs)
  {
    return file_type(fs) == S_IFREG;
  }

  bool
  is_dir(const file_status &fs)
  {
    return file_type(fs) == S_IFDIR;
  }

  bool
  is_readable(const path &p)
  {
    return 0 == access(p.c_str(), R_OK);
  }

  std::string
  load(const path &p, std::error_code &ec)
  {
    std::string zret;
    ats_scoped_fd fd(::open(p.c_str(), O_RDONLY));
    ec.clear();
    if (fd < 0) {
      ec = std::error_code(errno, std::system_category());
    } else {
      struct stat info;
      if (0 != ::fstat(fd, &info)) {
        ec = std::error_code(errno, std::system_category());
      } else {
        int n = info.st_size;
        zret.resize(n);
        auto read_len = ::read(fd, const_cast<char *>(zret.data()), n);
        if (read_len < n) {
          ec = std::error_code(errno, std::system_category());
        }
      }
    }
    return zret;
  }

} // namespace file
} // namespace ts
