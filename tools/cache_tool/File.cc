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

namespace ApacheTrafficServer {

  FilePath& FilePath::operator = (char const* path)
  {
    _path = ats_strdup(path);
    _stat_p = false;
    return *this;
  }

  bool FilePath::is_readable() const { return 0 == access(_path, R_OK); }

  FilePath operator / (FilePath const& lhs, FilePath const& rhs)
  {
    return static_cast<char const*>(lhs) / rhs;
  }

  FilePath operator / (char const* lhs, FilePath const& rhs)
  {
    ats_scoped_str np;

    // If either path is empty, return the other path.
    if (nullptr == lhs || 0 == *lhs) return rhs;
    if (!rhs.has_path()) return FilePath(lhs);

    return FilePath(path_join(lhs, static_cast<char const*>(rhs)));
  }

  ats_scoped_fd FilePath::open(int flags) const
  {
    return ats_scoped_fd(this->has_path() ? ::open(_path, flags) : ats_scoped_fd::Traits::initValue());
  }

  int
  BulkFile::load()
  {
    ats_scoped_fd fd(this->open(O_RDONLY));
    int zret = 0; // return errno if something goes wrong.
    struct stat info;
    if (0 == fstat(fd, &info)) {
      size_t n = info.st_size;
      _content = static_cast<char*>(ats_malloc(n+2));
      if (0 < (_len = read(fd, _content, n))) {
        // Force a trailing linefeed and nul.
        memset(_content + _len, 0, 2);
        if (_content[n-1] != '\n') {
          _content[n] = '\n';
          ++_len;
        }
      } else zret = errno;
    } else zret = errno;
    return zret;
  }

  StringView
  BulkFile::content() const
  {
    return StringView(_content, _len);
  }
}
