/** @file

    File support classes.

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

#include "File.h"
#include <unistd.h>
#include <fcntl.h>

namespace ts
{
/** Combine two strings as file paths.
     Trailing and leading separators for @a lhs and @a rhs respectively
     are handled to yield exactly one separator.
     @return A newly @x ats_malloc string of the combined paths.
*/
std::string
path_join(ts::string_view lhs, ts::string_view rhs)
{
  size_t ln        = lhs.size();
  size_t rn        = rhs.size();
  const char *rptr = rhs.data(); // May need to be modified.

  if (ln && lhs[ln - 1] == '/') {
    --ln; // drop trailing separator.
  }
  if (rn && *rptr == '/') {
    --rn, ++rptr; // drop leading separator.
  }

  std::string x;
  x.resize(ln + rn + 2);

  memcpy(const_cast<char *>(x.data()), lhs.data(), ln);
  x[ln] = '/';
  memcpy(const_cast<char *>(x.data()) + ln + 1, rptr, rn);
  x[ln + rn + 1] = 0; // terminate string.

  return x;
}

FilePath &
FilePath::operator=(char const *path)
{
  _path   = path;
  _stat_p = STAT_P::UNDEF;
  return *this;
}

bool
FilePath::is_readable() const
{
  return 0 == access(_path.c_str(), R_OK);
}

FilePath
operator/(FilePath const &lhs, FilePath const &rhs)
{
  return static_cast<char const *>(lhs) / rhs;
}

FilePath
operator/(char const *lhs, FilePath const &rhs)
{
  ats_scoped_str np;

  // If either path is empty, return the other path.
  if (nullptr == lhs || 0 == *lhs) {
    return rhs;
  }
  if (!rhs.has_path()) {
    return FilePath(lhs);
  }

  return FilePath(path_join(lhs, static_cast<const char *>(rhs)));
}

ats_scoped_fd
FilePath::open(int flags) const
{
  return ats_scoped_fd(this->has_path() ? ::open(_path.c_str(), flags) : ats_scoped_fd());
}

int
BulkFile::load()
{
  ats_scoped_fd fd(this->open(O_RDONLY));
  int zret = 0; // return errno if something goes wrong.
  struct stat info;
  if (0 == fstat(fd, &info)) {
    size_t n = info.st_size;
    _content.resize(n + 2);
    auto data     = const_cast<char *>(_content.data());
    auto read_len = read(fd, data, n);
    if (0 < read_len) {
      _len = read_len;
      // Force a trailing linefeed and nul.
      memset(data + _len, 0, 2);
      if (data[n - 1] != '\n') {
        data[n] = '\n';
        ++_len;
      }
    } else {
      zret = errno;
    }
  } else {
    zret = errno;
  }
  return zret;
}

TextView
BulkFile::content() const
{
  return {_content.data(), _len};
}
} // namespace ts
