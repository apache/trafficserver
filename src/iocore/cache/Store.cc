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

#include "P_Cache.h"

#include "iocore/cache/Store.h"
#include "iocore/cache/YamlStorageConfig.h"

#include "tscore/ink_memory.h"
#include "tscore/ink_platform.h"
#include "tscore/Layout.h"
#include "tscore/Filenames.h"
#include "tscore/ink_file.h"
#include "tscore/SimpleTokenizer.h"

#if defined(__linux__)
#include <linux/major.h>
#endif

#include <string>

//
// Store
//

namespace
{

DbgCtl dbg_ctl_cache_init{"cache_init"};

} // end anonymous namespace

static span_error_t
make_span_error(int error)
{
  switch (error) {
  case ENOENT:
    return SPAN_ERROR_NOT_FOUND;
  case EPERM: /* fallthrough */
  case EACCES:
    return SPAN_ERROR_NO_ACCESS;
  default:
    return SPAN_ERROR_UNKNOWN;
  }
}

static const char *
span_file_typename(mode_t st_mode)
{
  switch (st_mode & S_IFMT) {
  case S_IFBLK:
    return "block device";
  case S_IFCHR:
    return "character device";
  case S_IFDIR:
    return "directory";
  case S_IFREG:
    return "file";
  default:
    return "<unsupported>";
  }
}

void
Store::sort()
{
  Span **vec = static_cast<Span **>(alloca(sizeof(Span *) * n_spans));
  memset(vec, 0, sizeof(Span *) * n_spans);
  for (unsigned i = 0; i < n_spans; i++) {
    vec[i]   = spans[i];
    spans[i] = nullptr;
  }

  // sort by device

  unsigned n = 0;
  for (unsigned i = 0; i < n_spans; i++) {
    for (Span *sd = vec[i]; sd; sd = vec[i]) {
      vec[i] = vec[i]->link.next;
      for (unsigned d = 0; d < n; d++) {
        if (sd->disk_id == spans[d]->disk_id) {
          sd->link.next = spans[d];
          spans[d]      = sd;
          goto Ldone;
        }
      }
      spans[n++] = sd;
    Ldone:;
    }
  }
  n_spans = n;

  // sort by pathname x offset

  for (unsigned i = 0; i < n_spans; i++) {
  Lagain:
    Span *prev = nullptr;
    for (Span *sd = spans[i]; sd;) {
      Span *next = sd->link.next;
      if (next &&
          ((strcmp(sd->pathname, next->pathname) < 0) || (!strcmp(sd->pathname, next->pathname) && sd->offset > next->offset))) {
        if (!prev) {
          spans[i]        = next;
          sd->link.next   = next->link.next;
          next->link.next = sd;
        } else {
          prev->link.next = next;
          sd->link.next   = next->link.next;
          next->link.next = sd;
        }
        goto Lagain;
      }
      prev = sd;
      sd   = next;
    }
  }

  // merge adjacent spans

  for (unsigned i = 0; i < n_spans; i++) {
    for (Span *sd = spans[i]; sd;) {
      Span *next = sd->link.next;
      if (next && !strcmp(sd->pathname, next->pathname)) {
        if (!sd->file_pathname) {
          sd->blocks += next->blocks;
        } else if (next->offset <= sd->end()) {
          if (next->end() >= sd->end()) {
            sd->blocks += (next->end() - sd->end());
          }
        } else {
          sd = next;
          continue;
        }
        sd->link.next = next->link.next;
        delete next;
        sd = sd->link.next;
      } else {
        sd = next;
      }
    }
  }
}

const char *
Span::errorstr(span_error_t serr)
{
  switch (serr) {
  case SPAN_ERROR_OK:
    return "no error";
  case SPAN_ERROR_NOT_FOUND:
    return "file not found";
  case SPAN_ERROR_NO_ACCESS:
    return "unable to access file";
  case SPAN_ERROR_MISSING_SIZE:
    return "missing size specification";
  case SPAN_ERROR_UNSUPPORTED_DEVTYPE:
    return "unsupported cache file type";
  case SPAN_ERROR_MEDIA_PROBE:
    return "failed to probe device geometry";
  case SPAN_ERROR_UNKNOWN: /* fallthrough */
  default:
    return "unknown error";
  }
}

void
Span::hash_base_string_set(const char *s)
{
  hash_base_string = s ? ats_strdup(s) : nullptr;
}

void
Store::delete_all()
{
  for (unsigned i = 0; i < n_spans; i++) {
    if (spans[i]) {
      delete spans[i];
    }
  }
  n_spans = 0;
  ats_free(spans);
  spans = nullptr;
}

Store::~Store()
{
  delete_all();
}

Span::~Span()
{
  if (link.next) {
    delete link.next;
  }
}

extern ConfigVolumes config_volumes;

Result
Store::read_config()
{
  SpanConfig storage_config;
  ats_scoped_str storage_path(RecConfigReadConfigPath(nullptr, ts::filename::STORAGE));

  Note("%s loading ...", ts::filename::STORAGE);

  YamlStorageConfig::load(storage_config, config_volumes, storage_path.get());

  this->n_spans_in_config = storage_config.size();

  int n_dsstore = 0;
  Span *prev    = nullptr;
  Span *head    = nullptr;

  for (const auto &it : storage_config) {
    Span *span = new Span();

    Dbg(dbg_ctl_cache_init, "Span id=\"%s\" path=\"%s\" size=%" PRId64 " hash_seed=%s", it.id.c_str(), it.path.c_str(), it.size,
        it.hash_seed.c_str());

    std::string pp  = Layout::get()->relative(it.path);
    const char *err = span->init(it.id.c_str(), pp.c_str(), it.size);
    if (err) {
      Dbg(dbg_ctl_cache_init, "Store::read_config - could not initialize storage \"%s\" [%s]", pp.c_str(), err);
      delete span;
      continue;
    }

    n_dsstore++;

    // Set side values if present.
    if (!it.hash_seed.empty()) {
      span->hash_base_string_set(it.hash_seed.c_str());
    }

    // link prev and current Span
    if (prev == nullptr) {
      head = span;
    } else {
      prev->link.next = span;
    }
    prev = span;
  }

  // count the number of disks
  extend(n_dsstore);
  Span *s = head;
  for (int i = 0; i < n_dsstore; ++i) {
    Span *next   = s->link.next;
    s->link.next = nullptr;
    spans[i]     = s;
    s            = next;
  }
  sort();

  // print read config_volumes
  if (dbg_ctl_cache_init.on()) {
    for (ConfigVol *conf = config_volumes.cp_queue.head; conf != nullptr; conf = config_volumes.cp_queue.next(conf)) {
      if (conf->spans.empty()) {
        if (conf->size.in_percent) {
          Dbg(dbg_ctl_cache_init, "Volume number=%d size=%d%%", conf->number, conf->size.percent);
        } else {
          Dbg(dbg_ctl_cache_init, "Volume number=%d size=%" PRId64, conf->number, conf->size.absolute_value);
        }
      } else {
        Dbg(dbg_ctl_cache_init, "Volume number=%d", conf->number);
        for (auto &span_conf : conf->spans) {
          if (span_conf.size.in_percent) {
            Dbg(dbg_ctl_cache_init, "  using span id=%s size=%d%%", span_conf.use.c_str(), span_conf.size.percent);
          } else {
            Dbg(dbg_ctl_cache_init, "  using span id=%s size=%" PRId64, span_conf.use.c_str(), span_conf.size.absolute_value);
          }
        }
      }
    }
  }

  Note("%s finished loading", ts::filename::STORAGE);

  return Result::ok();
}

int
Store::write_config_data(int fd) const
{
  for (unsigned i = 0; i < n_spans; i++) {
    for (Span *sd = spans[i]; sd; sd = sd->link.next) {
      char buf[PATH_NAME_MAX + 64];
      snprintf(buf, sizeof(buf), "%s %" PRId64 "\n", sd->pathname.get(), sd->blocks * static_cast<int64_t>(STORE_BLOCK_SIZE));
      if (ink_file_fd_writestring(fd, buf) == -1) {
        return (-1);
      }
    }
  }
  return 0;
}

const char *
Span::init(const char *span_id, const char *path, int64_t size)
{
  this->id = ats_strdup(span_id);

  struct stat sbuf;
  struct statvfs vbuf;
  span_error_t serr;
  ink_device_geometry geometry;

  ats_scoped_fd fd(SocketManager::open(path, O_RDONLY));
  if (fd < 0) {
    serr = make_span_error(errno);
    Warning("unable to open '%s': %s", path, strerror(errno));
    goto fail;
  }

  if (fstat(fd, &sbuf) == -1) {
    serr = make_span_error(errno);
    Warning("unable to stat '%s': %s (%d)", path, strerror(errno), errno);
    goto fail;
  }

  if (fstatvfs(fd, &vbuf) == -1) {
    serr = make_span_error(errno);
    Warning("unable to statvfs '%s': %s (%d)", path, strerror(errno), errno);
    goto fail;
  }

  // Directories require an explicit size parameter. For device nodes and files, we use
  // the existing size.
  if (S_ISDIR(sbuf.st_mode)) {
    if (size <= 0) {
      Warning("cache %s '%s' requires a size > 0", span_file_typename(sbuf.st_mode), path);
      serr = SPAN_ERROR_MISSING_SIZE;
      goto fail;
    }
  }

  // Should regular files use the IO size (vbuf.f_bsize), or the
  // fundamental filesystem block size (vbuf.f_frsize)? That depends
  // on whether we are using that block size for performance or for
  // reliability.

  switch (sbuf.st_mode & S_IFMT) {
  case S_IFBLK:
  case S_IFCHR:

#if defined(__linux__)
    if (major(sbuf.st_rdev) == RAW_MAJOR && minor(sbuf.st_rdev) == 0) {
      Warning("cache %s '%s' is the raw device control interface", span_file_typename(sbuf.st_mode), path);
      serr = SPAN_ERROR_UNSUPPORTED_DEVTYPE;
      goto fail;
    }
#endif

    if (!ink_file_get_geometry(fd, geometry)) {
      serr = make_span_error(errno);

      if (errno == ENOTSUP) {
        Warning("failed to query disk geometry for '%s', no raw device support", path);
      } else {
        Warning("failed to query disk geometry for '%s', %s (%d)", path, strerror(errno), errno);
      }

      goto fail;
    }

    this->disk_id[0]     = 0;
    this->disk_id[1]     = sbuf.st_rdev;
    this->file_pathname  = true;
    this->hw_sector_size = geometry.blocksz;
    this->alignment      = geometry.alignsz;
    this->blocks         = geometry.totalsz / STORE_BLOCK_SIZE;

    break;

  case S_IFDIR:
    if (static_cast<int64_t>(vbuf.f_frsize * vbuf.f_bavail) < size) {
      Warning("not enough free space for cache %s '%s'", span_file_typename(sbuf.st_mode), path);
      // Just warn for now; let the cache open fail later.
    }

    // The cache initialization code in Cache.cc takes care of creating the actual cache file, naming it and making
    // it the right size based on the "file_pathname" flag. That's something that we should clean up in the future.
    this->file_pathname = false;

    this->disk_id[0]     = sbuf.st_dev;
    this->disk_id[1]     = sbuf.st_ino;
    this->hw_sector_size = vbuf.f_bsize;
    this->alignment      = 0;
    this->blocks         = size / STORE_BLOCK_SIZE;
    break;

  case S_IFREG:
    if (size > 0 && sbuf.st_size < size) {
      int64_t needed = size - sbuf.st_size;
      if (static_cast<int64_t>(vbuf.f_frsize * vbuf.f_bavail) < needed) {
        Warning("not enough free space for cache %s '%s'", span_file_typename(sbuf.st_mode), path);
        // Just warn for now; let the cache open fail later.
      }
    }

    this->disk_id[0]     = sbuf.st_dev;
    this->disk_id[1]     = sbuf.st_ino;
    this->file_pathname  = true;
    this->hw_sector_size = vbuf.f_bsize;
    this->alignment      = 0;
    this->blocks         = sbuf.st_size / STORE_BLOCK_SIZE;

    break;

  default:
    serr = SPAN_ERROR_UNSUPPORTED_DEVTYPE;
    goto fail;
  }

  // The actual size of a span always trumps the configured size.
  if (size > 0 && this->size() != size) {
    int64_t newsz = std::min(size, this->size());

    Note("cache %s '%s' is %" PRId64 " bytes, but the configured size is %" PRId64 " bytes, using the minimum",
         span_file_typename(sbuf.st_mode), path, this->size(), size);

    this->blocks = newsz / STORE_BLOCK_SIZE;
  }

  // A directory span means we will end up with a file, otherwise, we get what we asked for.
  this->pathname = ats_strdup(path);

  Dbg(dbg_ctl_cache_init, "initialized span '%s'", this->pathname.get());
  Dbg(dbg_ctl_cache_init,
      "hw_sector_size=%d, size=%" PRId64 ", blocks=%" PRId64 ", disk_id=%" PRId64 "/%" PRId64 ", file_pathname=%d",
      this->hw_sector_size, this->size(), this->blocks, this->disk_id[0], this->disk_id[1], this->file_pathname);

  return nullptr;

fail:
  return Span::errorstr(serr);
}
