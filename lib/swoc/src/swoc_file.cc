// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Minimalist version of std::filesystem.
 */

#include <variant>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
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

path
path::relative_path() const {
  if (!_path.empty() && _path.front() == SEPARATOR) {
    return _path.substr(1);
  }
  return *this;
}

auto
path::filename() const -> self_type {
  auto const idx = _path.find_last_of(SEPARATOR);
  return idx == std::string::npos ? self_type(_path) : _path.substr(idx + 1);
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

void
file_status::init() {
  switch (_stat.st_mode & S_IFMT) {
  case S_IFREG:
    _type = file_type::regular;
    break;
  case S_IFDIR:
    _type = file_type::directory;
    break;
  case S_IFLNK:
    _type = file_type::symlink;
    break;
  case S_IFBLK:
    _type = file_type::block;
    break;
  case S_IFCHR:
    _type = file_type::character;
    break;
  case S_IFIFO:
    _type = file_type::fifo;
    break;
  case S_IFSOCK:
    _type = file_type::socket;
    break;
  default:
    _type = file_type::unknown;
    break;
  }
}

file_status
status(path const &file, std::error_code &ec) noexcept {
  file_status zret;
  if (::stat(file.c_str(), &zret._stat) >= 0) {
    ec.clear();
    zret.init();
  } else {
    ec = std::error_code(errno, std::system_category());
    if (errno == ENOENT) {
      zret._type = file_type::not_found;
    }
  }
  return zret;
}

int
file_type(const file_status &fs) {
  return fs._stat.st_mode & S_IFMT;
}

uintmax_t
file_size(const file_status &fs) {
  return fs._stat.st_size;
}

bool
exists(const path &p) {
  std::error_code ec;
  auto fs = status(p, ec);
  return exists(fs);
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

inline file_time_type
chrono_cast(timespec const &ts) {
  using namespace std::chrono;
  return system_clock::time_point{duration_cast<system_clock::duration>(seconds{ts.tv_sec} + nanoseconds{ts.tv_nsec})};
}

// Apple has different names for these members, need to have accessors that account for that.
// Under -O2 these are completely elided.
template <typename S>
auto
a_time(S const &s, meta::CaseTag<0>) -> decltype(S::st_atim) {
  return s.st_atim;
}

template <typename S>
auto
a_time(S const &s, meta::CaseTag<1>) -> decltype(S::st_atimespec) {
  return s.st_atimespec;
}

template <typename S>
auto
m_time(S const &s, meta::CaseTag<0>) -> decltype(S::st_mtim) {
  return s.st_mtim;
}

template <typename S>
auto
m_time(S const &s, meta::CaseTag<1>) -> decltype(S::st_mtimespec) {
  return s.st_mtimespec;
}

template <typename S>
auto
c_time(S const &s, meta::CaseTag<0>) -> decltype(S::st_ctim) {
  return s.st_ctim;
}

template <typename S>
auto
c_time(S const &s, meta::CaseTag<1>) -> decltype(S::st_ctimespec) {
  return s.st_ctimespec;
}

} // namespace

file_time_type
last_write_time(file_status const &fs) {
  return chrono_cast(m_time(fs._stat, meta::CaseArg));
}

file_time_type
access_time(file_status const &fs) {
  return chrono_cast(a_time(fs._stat, meta::CaseArg));
}

file_time_type
status_time(file_status const &fs) {
  return chrono_cast(c_time(fs._stat, meta::CaseArg));
}

file_time_type
last_write_time(path const &p, std::error_code &ec) {
  auto fs = status(p, ec);
  if (ec) {
    return file_time_type::min();
  }
  return last_write_time(fs);
}

bool
is_readable(const path &p) {
  return 0 == access(p.c_str(), R_OK);
}

path
temp_directory_path() {
  /* ISO/IEC 9945 (POSIX): The path supplied by the first environment variable found in the list TMPDIR, TMP, TEMP, TEMPDIR.
   * If none of these are found, "/tmp"
   * */
  for (char const *tp : {"TMPDIR", "TMP", "TEMPDIR"}) {
    if (auto v = ::getenv(tp); v) {
      return path(v);
    }
  }
  return path("/tmp");
}

path
current_path() {
  char buff[PATH_MAX + 1];
  if (auto p = ::getcwd(buff, sizeof(buff)); p) {
    return path{buff};
#if !__FreeBSD__ && !__APPLE__ // Freakin' Apple and FreeBSD.
  } else if (ERANGE == errno) {
    swoc::unique_malloc<char> raw{::get_current_dir_name()};
    return path{raw.get()};
#endif
  }
  return {};
}

path
canonical(const path &p, std::error_code &ec) {
  if (p.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
    return {};
  }

  char buf[PATH_MAX + 1];

  if (auto rp = ::realpath(p.c_str(), buf); rp) {
    return path{rp};
  }

  if (auto rp = ::realpath(p.c_str(), nullptr); rp) {
    return path{rp};
  }

  ec = std::error_code(errno, std::system_category());
  return {};
}

bool
create_directory(const path &path, std::error_code &ec, mode_t mode) noexcept {
  if (path.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
    return false;
  }

  ec.clear();
  if (::mkdir(path.c_str(), mode) != 0) {
    if (EEXIST == errno) {
      std::error_code local_ec;
      auto fs = status(path, local_ec);
      if (!local_ec && is_dir(fs)) {
        return true;
      }
    }
    ec = std::error_code(errno, std::system_category());
    return false;
  }
  return true;
}

bool
create_directories(const path &p, std::error_code &ec, mode_t mode) noexcept {
  TextView text(p.string());

  if (text.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
    return false;
  }

  path path;

  if (text.front() == path::SEPARATOR) {
    path = text.prefix(1); // copy leading separator.
    ++text;
    if (!text) {
      ec.clear();
      return true; // Tried to create root directory, it's already tehre.
    }
  }

  path.reserve(p.string().size());

  while (text) {
    auto elt  = text.take_prefix_at(path::SEPARATOR);
    path     /= elt;
    if (!create_directory(path, ec, mode)) {
      return false;
    }
  }

  return true;
}

bool
copy(const path &from, const path &to, std::error_code &ec) {
  static constexpr size_t BUF_SIZE = 65536;
  std::error_code local_ec;
  char buf[BUF_SIZE];
  swoc::MemSpan span{buf};

  if (from.empty() || to.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
    return false;
  }

  ec.clear();

  unique_fd src_fd{::open(from.c_str(), O_RDONLY)};
  if (NO_FD == src_fd) {
    ec = std::error_code(errno, std::system_category());
    return false;
  }
  auto src_fs = file::status(from, local_ec);

  path final_to;
  if (auto fs = file::status(to, local_ec); !(local_ec && ENOENT == local_ec.value()) && is_dir(fs)) {
    final_to = to / from.filename();
  } else {
    final_to = to;
  }

  unique_fd dst_fd{::open(final_to.c_str(), O_WRONLY | O_CREAT, src_fs.mode())};
  if (NO_FD == dst_fd) {
    ec = std::error_code(errno, std::system_category());
    return false;
  }

  while (true) {
    if (auto n = read(src_fd, span.data(), span.size()); n > 0) {
      if (::write(dst_fd, span.data(), n) < n) {
        ec = std::error_code(errno, std::system_category());
        break;
      }
    } else {
      break;
    }
  }

  return true;
}

uintmax_t
remove_all(const path &p, std::error_code &ec) {
  // coverity TOCTOU - issue is doing stat before doing operation. Stupid complaint, ignore.
  DIR *dir             = nullptr;
  struct dirent *entry = nullptr;
  std::error_code err;
  uintmax_t zret = 0;

  struct ::stat s {};
  if (p.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
    return zret;
  } else if (::stat(p.c_str(), &s) < 0) {
    ec = std::error_code(errno, std::system_category());
    return zret;
  } else if (S_ISREG(s.st_mode)) { // regular file, try to remove it!
    // coverity[toctou : SUPPRESS]
    if (unlink(p.c_str()) != 0) {
      ec = std::error_code(errno, std::system_category());
    } else {
      ++zret;
    }
    return zret;
  } else if (!S_ISDIR(s.st_mode)) { // not a directory
    ec = std::error_code(ENOTDIR, std::system_category());
    return zret;
  }
  // Invariant - @a p is a directory.

  // recursively remove nested files and directories
  // coverity[toctou : SUPPRESS]
  if (nullptr == (dir = opendir(p.c_str()))) {
    ec = std::error_code(errno, std::system_category());
    return zret;
  }

  auto child = p; // Minimize string allocations / re-allocations.
  while (nullptr != (entry = readdir(dir))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    child  = p;
    child /= entry->d_name;
    zret  += remove_all(child, ec);
  }

  if (0 != rmdir(p.c_str())) {
    ec = std::error_code(errno, std::system_category());
  }
  ++zret;

  closedir(dir);
  return zret;
}

bool
remove(path const &p, std::error_code &ec) {
  // coverity TOCTOU - issue is doing stat before doing operation. Stupid complaint, ignore.
  struct ::stat fs {};
  if (p.empty()) {
    ec = std::error_code(EINVAL, std::system_category());
  } else if (::stat(p.c_str(), &fs) < 0) {
    ec = std::error_code(errno, std::system_category());
  } else if (S_ISREG(fs.st_mode)) { // regular file, try to remove it!
    // coverity[toctou : SUPPRESS]
    if (unlink(p.c_str()) != 0) {
      ec = std::error_code(errno, std::system_category());
    }
  } else if (S_ISDIR(fs.st_mode)) { // not a directory
    // coverity[toctou : SUPPRESS]
    if (rmdir(p.c_str()) != 0) {
      ec = std::error_code(errno, std::system_category());
    }
  } else {
    ec = std::error_code(EINVAL, std::system_category());
  }
  return !ec;
}

std::string
load(const path &p, std::error_code &ec) {
  std::string zret;
  ec.clear();
  if (unique_fd fd(::open(p.c_str(), O_RDONLY)); fd < 0) {
    ec = std::error_code(errno, std::system_category());
  } else {
    struct stat info {};
    if (0 != ::fstat(fd, &info)) {
      ec = std::error_code(errno, std::system_category());
    } else {
      auto n = info.st_size;
      zret.resize(n);
      auto read_len = ::read(fd, zret.data(), n);
      if (read_len < n) {
        ec = std::error_code(errno, std::system_category());
      }
    }
  }
  return zret;
}

} // namespace file

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, file::path const &p) {
  return bwformat(w, spec, p.string());
}

}} // namespace swoc::SWOC_VERSION_NS
