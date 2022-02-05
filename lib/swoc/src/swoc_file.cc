// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Minimalist version of std::filesystem.
 */

#include <fcntl.h>
#include <unistd.h>
#include "swoc/swoc_file.h"
#include "swoc/bwf_base.h"

using namespace swoc::literals;

namespace swoc { inline namespace SWOC_VERSION_NS {
namespace file {
path
path::parent_path() const {
  TextView parent{_path};
  parent.split_suffix_at(SEPARATOR);
  return parent ? parent : "/"_tv;
}

path &
path::operator/=(std::string_view that) {
  if (!that.empty()) { // don't waste time appending nothing.
    if (that.front() == SEPARATOR || _path.empty()) {
      _path.assign(that);
    } else {
      if (_path.back() == SEPARATOR) {
        _path.reserve(_path.size() + that.size());
      } else {
        _path.reserve(_path.size() + that.size() + 1);
        _path.push_back(SEPARATOR);
      }
      _path.append(that);
    }
  }
  return *this;
}

file_status
status(path const &file, std::error_code &ec) noexcept {
  file_status zret;
  if (::stat(file.c_str(), &zret._stat) >= 0) {
    ec.clear();
  } else {
    ec = std::error_code(errno, std::system_category());
  }
  return zret;
}

int
file_type(const file_status &fs) {
  return fs._stat.st_mode & S_IFMT;
}

off_t
file_size(const file_status &fs) {
  return fs._stat.st_size;
}

bool
is_char_device(const file_status &fs) {
  return file_type(fs) == S_IFCHR;
}

bool
is_block_device(const file_status &fs) {
  return file_type(fs) == S_IFBLK;
}

bool
is_regular_file(const file_status &fs) {
  return file_type(fs) == S_IFREG;
}

bool
is_dir(const file_status &fs) {
  return file_type(fs) == S_IFDIR;
}

path
absolute(path const &src, std::error_code &ec) {
  char buff[4096];
  ec.clear();
  if (src.is_absolute()) {
    return src;
  }
  auto s = realpath(src.c_str(), buff);
  if (s == nullptr) {
    if (errno == ENAMETOOLONG) {
      s = realpath(src.c_str(), nullptr);
      if (s != nullptr) {
        path zret{s};
        free(s);
        return zret;
      }
    }
    ec = std::error_code(errno, std::system_category());
    return {};
  }
  return path{s};
}

namespace {
inline std::chrono::system_clock::time_point
chrono_cast(timespec const &ts) {
  using namespace std::chrono;
  return system_clock::time_point{duration_cast<system_clock::duration>(seconds{ts.tv_sec} + nanoseconds{ts.tv_nsec})};
}

// Apple has different names for these members, need to have accessors that account for that.
// Under -O2 these are completely elided.
template <typename S>
auto
a_time(S const &s) -> decltype(S::st_atim) {
  return s.st_atim;
}

template <typename S>
auto
a_time(S const &s) -> decltype(S::st_atimespec) {
  return s.st_atimespec;
}

template <typename S>
auto
m_time(S const &s) -> decltype(S::st_mtim) {
  return s.st_mtim;
}

template <typename S>
auto
m_time(S const &s) -> decltype(S::st_mtimespec) {
  return s.st_mtimespec;
}

template <typename S>
auto
c_time(S const &s) -> decltype(S::st_ctim) {
  return s.st_ctim;
}

template <typename S>
auto
c_time(S const &s) -> decltype(S::st_ctimespec) {
  return s.st_ctimespec;
}

} // namespace

std::chrono::system_clock::time_point
modify_time(file_status const &fs) {
  return chrono_cast(m_time(fs._stat));
}

std::chrono::system_clock::time_point
access_time(file_status const &fs) {
  return chrono_cast(a_time(fs._stat));
}

std::chrono::system_clock::time_point
status_time(file_status const &fs) {
  return chrono_cast(c_time(fs._stat));
}

bool
is_readable(const path &p) {
  return 0 == access(p.c_str(), R_OK);
}

std::string
load(const path &p, std::error_code &ec) {
  std::string zret;
  int fd(::open(p.c_str(), O_RDONLY));
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
    ::close(fd);
  }
  return zret;
}

} // namespace file

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, file::path const &p) {
  return bwformat(w, spec, p.string());
}

}} // namespace swoc::SWOC_VERSION_NS
