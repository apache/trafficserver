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

#include "tscore/ink_platform.h"
#include "P_Cache.h"
#include "tscore/I_Layout.h"
#include "tscore/Filenames.h"
#include "tscore/ink_file.h"
#include "tscore/Tokenizer.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/runroot.h"

#if HAVE_LINUX_MAJOR_H
#include <linux/major.h>
#endif

//
// Store
//
const char Store::VOLUME_KEY[]           = "volume";
const char Store::HASH_BASE_STRING_KEY[] = "id";

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

Ptr<ProxyMutex> tmp_p;
Store::Store() {}

void
Store::add(Span *ds)
{
  extend(n_disks + 1);
  disk[n_disks - 1] = ds;
}

void
Store::add(Store &s)
{
  // assume on different disks
  for (unsigned i = 0; i < s.n_disks; i++) {
    add(s.disk[i]);
  }
  s.n_disks = 0;
  s.delete_all();
}

// should be changed to handle offset in more general
// case (where this is not a free of a "just" allocated
// store
void
Store::free(Store &s)
{
  for (unsigned i = 0; i < s.n_disks; i++) {
    for (Span *sd = s.disk[i]; sd; sd = sd->link.next) {
      for (unsigned j = 0; j < n_disks; j++) {
        for (Span *d = disk[j]; d; d = d->link.next) {
          if (!strcmp(sd->pathname, d->pathname)) {
            if (sd->offset < d->offset) {
              d->offset = sd->offset;
            }
            d->blocks += sd->blocks;
            goto Lfound;
          }
        }
      }
      ink_release_assert(!"Store::free failed");
    Lfound:;
    }
  }
}

void
Store::sort()
{
  Span **vec = static_cast<Span **>(alloca(sizeof(Span *) * n_disks));
  memset(vec, 0, sizeof(Span *) * n_disks);
  for (unsigned i = 0; i < n_disks; i++) {
    vec[i]  = disk[i];
    disk[i] = nullptr;
  }

  // sort by device

  unsigned n = 0;
  for (unsigned i = 0; i < n_disks; i++) {
    for (Span *sd = vec[i]; sd; sd = vec[i]) {
      vec[i] = vec[i]->link.next;
      for (unsigned d = 0; d < n; d++) {
        if (sd->disk_id == disk[d]->disk_id) {
          sd->link.next = disk[d];
          disk[d]       = sd;
          goto Ldone;
        }
      }
      disk[n++] = sd;
    Ldone:;
    }
  }
  n_disks = n;

  // sort by pathname x offset

  for (unsigned i = 0; i < n_disks; i++) {
  Lagain:
    Span *prev = nullptr;
    for (Span *sd = disk[i]; sd;) {
      Span *next = sd->link.next;
      if (next &&
          ((strcmp(sd->pathname, next->pathname) < 0) || (!strcmp(sd->pathname, next->pathname) && sd->offset > next->offset))) {
        if (!prev) {
          disk[i]         = next;
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

  for (unsigned i = 0; i < n_disks; i++) {
    for (Span *sd = disk[i]; sd;) {
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

int
Span::path(char *filename, int64_t *aoffset, char *buf, int buflen)
{
  ink_assert(!aoffset);
  Span *ds = this;

  if ((strlen(ds->pathname) + strlen(filename) + 2) > static_cast<size_t>(buflen)) {
    return -1;
  }
  if (!ds->file_pathname) {
    ink_filepath_make(buf, buflen, ds->pathname, filename);
  } else {
    ink_strlcpy(buf, ds->pathname, buflen);
  }

  return strlen(buf);
}

void
Span::hash_base_string_set(const char *s)
{
  hash_base_string = s ? ats_strdup(s) : nullptr;
}

void
Span::volume_number_set(int n)
{
  forced_volume_num = n;
}

void
Store::delete_all()
{
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i]) {
      delete disk[i];
    }
  }
  n_disks = 0;
  ats_free(disk);
  disk = nullptr;
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

static int
get_int64(int fd, int64_t &data)
{
  char buf[PATH_NAME_MAX];
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%" PRId64 "", &data) != 1) {
    return (-1);
  }
  return 0;
}

int
Store::remove(char *n)
{
  bool found = false;
Lagain:
  for (unsigned i = 0; i < n_disks; i++) {
    Span *p = nullptr;
    for (Span *sd = disk[i]; sd; sd = sd->link.next) {
      if (!strcmp(n, sd->pathname)) {
        found = true;
        if (p) {
          p->link.next = sd->link.next;
        } else {
          disk[i] = sd->link.next;
        }
        sd->link.next = nullptr;
        delete sd;
        goto Lagain;
      }
      p = sd;
    }
  }
  return found ? 0 : -1;
}

Result
Store::read_config()
{
  int n_dsstore   = 0;
  int ln          = 0;
  int i           = 0;
  const char *err = nullptr;
  Span *sd = nullptr, *cur = nullptr;
  Span *ns;
  ats_scoped_fd fd;
  ats_scoped_str storage_path(RecConfigReadConfigPath(nullptr, ts::filename::STORAGE));

  Note("%s loading ...", ts::filename::STORAGE);
  Debug("cache_init", "Store::read_config, fd = -1, \"%s\"", (const char *)storage_path);
  fd = ::open(storage_path, O_RDONLY);
  if (fd < 0) {
    Error("%s failed to load", ts::filename::STORAGE);
    return Result::failure("open %s: %s", (const char *)storage_path, strerror(errno));
  }

  // For each line

  char line[1024];
  int len;
  while ((len = ink_file_fd_readline(fd, sizeof(line), line)) > 0) {
    const char *path;
    const char *seed = nullptr;
    // update lines

    ++ln;

    // Because the SimpleTokenizer is a bit too simple, we have to normalize whitespace.
    for (char *spot = line, *limit = line + len; spot < limit; ++spot) {
      if (ParseRules::is_space(*spot)) {
        *spot = ' '; // force whitespace to literal space.
      }
    }
    SimpleTokenizer tokens(line, ' ', SimpleTokenizer::OVERWRITE_INPUT_STRING);

    // skip comments and blank lines
    path = tokens.getNext();
    if (nullptr == path || '#' == path[0]) {
      continue;
    }

    // parse
    Debug("cache_init", "Store::read_config: \"%s\"", path);
    ++n_disks_in_config;

    int64_t size   = -1;
    int volume_num = -1;
    const char *e;
    while (nullptr != (e = tokens.getNext())) {
      if (ParseRules::is_digit(*e)) {
        const char *end;
        if ((size = ink_atoi64(e, &end)) <= 0 || *end != '\0') {
          delete sd;
          Error("%s failed to load", ts::filename::STORAGE);
          return Result::failure("failed to parse size '%s'", e);
        }
      } else if (0 == strncasecmp(HASH_BASE_STRING_KEY, e, sizeof(HASH_BASE_STRING_KEY) - 1)) {
        e += sizeof(HASH_BASE_STRING_KEY) - 1;
        if ('=' == *e) {
          ++e;
        }
        if (*e && !ParseRules::is_space(*e)) {
          seed = e;
        }
      } else if (0 == strncasecmp(VOLUME_KEY, e, sizeof(VOLUME_KEY) - 1)) {
        e += sizeof(VOLUME_KEY) - 1;
        if ('=' == *e) {
          ++e;
        }
        if (!*e || !ParseRules::is_digit(*e) || 0 >= (volume_num = ink_atoi(e))) {
          delete sd;
          Error("%s failed to load", ts::filename::STORAGE);
          return Result::failure("failed to parse volume number '%s'", e);
        }
      }
    }

    std::string pp = Layout::get()->relative(path);

    ns = new Span;
    Debug("cache_init", "Store::read_config - ns = new Span; ns->init(\"%s\",%" PRId64 "), forced volume=%d%s%s", pp.c_str(), size,
          volume_num, seed ? " id=" : "", seed ? seed : "");
    if ((err = ns->init(pp.c_str(), size))) {
      RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "could not initialize storage \"%s\" [%s]", pp.c_str(), err);
      Debug("cache_init", "Store::read_config - could not initialize storage \"%s\" [%s]", pp.c_str(), err);
      delete ns;
      continue;
    }

    n_dsstore++;

    // Set side values if present.
    if (seed) {
      ns->hash_base_string_set(seed);
    }
    if (volume_num > 0) {
      ns->volume_number_set(volume_num);
    }

    // new Span
    {
      Span *prev = cur;
      cur        = ns;
      if (!sd) {
        sd = cur;
      } else {
        prev->link.next = cur;
      }
    }
  }

  // count the number of disks
  extend(n_dsstore);
  cur = sd;
  while (cur) {
    Span *next     = cur->link.next;
    cur->link.next = nullptr;
    disk[i++]      = cur;
    cur            = next;
  }
  sd = nullptr; // these are all used.
  sort();

  Note("%s finished loading", ts::filename::STORAGE);

  return Result::ok();
}

int
Store::write_config_data(int fd) const
{
  for (unsigned i = 0; i < n_disks; i++) {
    for (Span *sd = disk[i]; sd; sd = sd->link.next) {
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
Span::init(const char *path, int64_t size)
{
  struct stat sbuf;
  struct statvfs vbuf;
  span_error_t serr;
  ink_device_geometry geometry;

  ats_scoped_fd fd(socketManager.open(path, O_RDONLY));
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

#if defined(linux)
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
  this->set_mmapable(ink_file_is_mmappable(S_ISDIR(sbuf.st_mode) ? static_cast<mode_t>(S_IFREG) : sbuf.st_mode));
  this->pathname = ats_strdup(path);

  Debug("cache_init", "initialized span '%s'", this->pathname.get());
  Debug("cache_init", "hw_sector_size=%d, size=%" PRId64 ", blocks=%" PRId64 ", disk_id=%" PRId64 "/%" PRId64 ", file_pathname=%d",
        this->hw_sector_size, this->size(), this->blocks, this->disk_id[0], this->disk_id[1], this->file_pathname);

  return nullptr;

fail:
  return Span::errorstr(serr);
}

void
Store::normalize()
{
  unsigned ndisks = 0;
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i]) {
      disk[ndisks++] = disk[i];
    }
  }
  n_disks = ndisks;
}

static unsigned int
try_alloc(Store &target, Span *source, unsigned int start_blocks, bool one_only = false)
{
  unsigned int blocks = start_blocks;
  Span *ds            = nullptr;
  while (source && blocks) {
    if (source->blocks) {
      unsigned int a; // allocated
      if (blocks > source->blocks) {
        a = source->blocks;
      } else {
        a = blocks;
      }
      Span *d = new Span(*source);

      d->blocks    = a;
      d->link.next = ds;

      if (d->file_pathname) {
        source->offset += a;
      }
      source->blocks -= a;
      ds = d;
      blocks -= a;
      if (one_only) {
        break;
      }
    }
    source = source->link.next;
  }
  if (ds) {
    target.add(ds);
  }
  return start_blocks - blocks;
}

void
Store::spread_alloc(Store &s, unsigned int blocks, bool mmapable)
{
  //
  // Count the eligible disks..
  //
  int mmapable_disks = 0;
  for (unsigned k = 0; k < n_disks; k++) {
    if (disk[k]->is_mmapable()) {
      mmapable_disks++;
    }
  }

  int spread_over = n_disks;
  if (mmapable) {
    spread_over = mmapable_disks;
  }

  if (spread_over == 0) {
    return;
  }

  int disks_left = spread_over;

  for (unsigned i = 0; blocks && disks_left && i < n_disks; i++) {
    if (!(mmapable && !disk[i]->is_mmapable())) {
      unsigned int target = blocks / disks_left;
      if (blocks - target > total_blocks(i + 1)) {
        target = blocks - total_blocks(i + 1);
      }
      blocks -= try_alloc(s, disk[i], target);
      disks_left--;
    }
  }
}

void
Store::try_realloc(Store &s, Store &diff)
{
  for (unsigned i = 0; i < s.n_disks; i++) {
    Span *prev = nullptr;
    for (Span *sd = s.disk[i]; sd;) {
      for (unsigned j = 0; j < n_disks; j++) {
        for (Span *d = disk[j]; d; d = d->link.next) {
          if (!strcmp(sd->pathname, d->pathname)) {
            if (sd->offset >= d->offset && (sd->end() <= d->end())) {
              if (!sd->file_pathname || (sd->end() == d->end())) {
                d->blocks -= sd->blocks;
                goto Lfound;
              } else if (sd->offset == d->offset) {
                d->blocks -= sd->blocks;
                d->offset += sd->blocks;
                goto Lfound;
              } else {
                Span *x = new Span(*d);
                // d will be the first vol
                d->blocks    = sd->offset - d->offset;
                d->link.next = x;
                // x will be the last vol
                x->offset = sd->offset + sd->blocks;
                x->blocks -= x->offset - d->offset;
                goto Lfound;
              }
            }
          }
        }
      }
      {
        if (!prev) {
          s.disk[i] = s.disk[i]->link.next;
        } else {
          prev->link.next = sd->link.next;
        }
        diff.extend(i + 1);
        sd->link.next = diff.disk[i];
        diff.disk[i]  = sd;
        sd            = prev ? prev->link.next : s.disk[i];
        continue;
      }
    Lfound:;
      prev = sd;
      sd   = sd->link.next;
    }
  }
  normalize();
  s.normalize();
  diff.normalize();
}

//
// Stupid grab first available space allocator
//
void
Store::alloc(Store &s, unsigned int blocks, bool one_only, bool mmapable)
{
  unsigned int oblocks = blocks;
  for (unsigned i = 0; blocks && i < n_disks; i++) {
    if (!(mmapable && !disk[i]->is_mmapable())) {
      blocks -= try_alloc(s, disk[i], blocks, one_only);
      if (one_only && oblocks != blocks) {
        break;
      }
    }
  }
}

int
Span::write(int fd) const
{
  char buf[32];

  if (ink_file_fd_writestring(fd, (pathname ? pathname.get() : ")")) == -1) {
    return (-1);
  }
  if (ink_file_fd_writestring(fd, "\n") == -1) {
    return (-1);
  }

  snprintf(buf, sizeof(buf), "%" PRId64 "\n", blocks);
  if (ink_file_fd_writestring(fd, buf) == -1) {
    return (-1);
  }

  snprintf(buf, sizeof(buf), "%d\n", file_pathname);
  if (ink_file_fd_writestring(fd, buf) == -1) {
    return (-1);
  }

  snprintf(buf, sizeof(buf), "%" PRId64 "\n", offset);
  if (ink_file_fd_writestring(fd, buf) == -1) {
    return (-1);
  }

  snprintf(buf, sizeof(buf), "%d\n", static_cast<int>(is_mmapable()));
  if (ink_file_fd_writestring(fd, buf) == -1) {
    return (-1);
  }

  return 0;
}

int
Store::write(int fd, const char *name) const
{
  char buf[32];

  if (ink_file_fd_writestring(fd, name) == -1) {
    return (-1);
  }
  if (ink_file_fd_writestring(fd, "\n") == -1) {
    return (-1);
  }

  snprintf(buf, sizeof(buf), "%d\n", n_disks);
  if (ink_file_fd_writestring(fd, buf) == -1) {
    return (-1);
  }

  for (unsigned i = 0; i < n_disks; i++) {
    int n    = 0;
    Span *sd = nullptr;
    for (sd = disk[i]; sd; sd = sd->link.next) {
      n++;
    }

    snprintf(buf, sizeof(buf), "%d\n", n);
    if (ink_file_fd_writestring(fd, buf) == -1) {
      return (-1);
    }

    for (sd = disk[i]; sd; sd = sd->link.next) {
      if (sd->write(fd)) {
        return -1;
      }
    }
  }
  return 0;
}

int
Span::read(int fd)
{
  char buf[PATH_NAME_MAX], p[PATH_NAME_MAX];

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%s", p) != 1) {
    return (-1);
  }
  pathname = ats_strdup(p);
  if (get_int64(fd, blocks) < 0) {
    return -1;
  }

  int b = 0;
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d", &b) != 1) {
    return (-1);
  }
  file_pathname = (b ? true : false);

  if (get_int64(fd, offset) < 0) {
    return -1;
  }

  int tmp;
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d", &tmp) != 1) {
    return (-1);
  }
  set_mmapable(tmp != 0);

  return (0);
}

int
Store::read(int fd, char *aname)
{
  char *name = aname;
  char tname[PATH_NAME_MAX];
  char buf[PATH_NAME_MAX];
  if (!aname) {
    name = tname;
  }

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%s\n", name) != 1) {
    return (-1);
  }

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
    return (-1);
  }
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d\n", &n_disks) != 1) {
    return (-1);
  }

  disk = static_cast<Span **>(ats_malloc(sizeof(Span *) * n_disks));
  if (!disk) {
    return -1;
  }
  memset(disk, 0, sizeof(Span *) * n_disks);
  for (unsigned i = 0; i < n_disks; i++) {
    int n = 0;

    if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0) {
      return (-1);
    }
    // the above line will guarantee buf to be no longer than PATH_NAME_MAX
    // so the next statement should be a safe use of sscanf
    // coverity[secure_coding]
    if (sscanf(buf, "%d\n", &n) != 1) {
      return (-1);
    }

    Span *sd = nullptr;
    while (n--) {
      Span *last = sd;
      sd         = new Span;

      if (!last) {
        disk[i] = sd;
      } else {
        last->link.next = sd;
      }
      if (sd->read(fd)) {
        goto Lbail;
      }
    }
  }
  return 0;
Lbail:
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i]) {
      delete disk[i];
    }
  }
  return -1;
}

Span *
Span::dup()
{
  Span *ds = new Span(*this);
  if (this->link.next) {
    ds->link.next = this->link.next->dup();
  }
  return ds;
}

void
Store::dup(Store &s)
{
  s.n_disks = n_disks;
  s.disk    = static_cast<Span **>(ats_malloc(sizeof(Span *) * n_disks));
  for (unsigned i = 0; i < n_disks; i++) {
    s.disk[i] = disk[i]->dup();
  }
}

int
Store::clear(char *filename, bool clear_dirs)
{
  char z[STORE_BLOCK_SIZE];
  memset(z, 0, STORE_BLOCK_SIZE);
  for (unsigned i = 0; i < n_disks; i++) {
    Span *ds = disk[i];
    for (unsigned j = 0; j < disk[i]->paths(); j++) {
      char path[PATH_NAME_MAX];
      Span *d = ds->nth(j);
      if (!clear_dirs && !d->file_pathname) {
        continue;
      }
      int r = d->path(filename, nullptr, path, PATH_NAME_MAX);
      if (r < 0) {
        return -1;
      }
      int fd = ::open(path, O_RDWR | O_CREAT, 0644);
      if (fd < 0) {
        return -1;
      }
      for (int b = 0; d->blocks; b++) {
        if (socketManager.pwrite(fd, z, STORE_BLOCK_SIZE, d->offset + (b * STORE_BLOCK_SIZE)) < 0) {
          close(fd);
          return -1;
        }
      }
      close(fd);
    }
  }
  return 0;
}
