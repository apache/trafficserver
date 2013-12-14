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

#include "libts.h"
#include "P_Cache.h"
#include "I_Layout.h"

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#if HAVE_SYS_DISK_H
#include <sys/disk.h>
#endif

#if HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif

// Global
Store theStore;

int Store::getVolume(char* line) {
  int v = 0;
  if(!line) return 0;
  char* str = strstr(line, vol_str);
  char* vol_start = str;
  if(!str) return 0;
  while (*str && !ParseRules::is_digit(*str))
    str++;
  v = ink_atoi(str);

  while (*str && ParseRules::is_digit(*str))
    str++;
  while(*str) {
    *vol_start = *str;
    vol_start++;
    str++;
  }
  *vol_start = 0;
  Debug("cache_init", "returning %d and '%s'", v, line);

  if(v < 0) return 0;
  return v;
}


//
// Store
//
Ptr<ProxyMutex> tmp_p;
Store::Store():n_disks(0), disk(NULL),
#if TS_USE_INTERIM_CACHE == 1
  n_interim_disks(0), interim_disk(NULL),
#endif
  vol_str("volume=")
{
}

int
initialize_store()
{
  return theStore.read_config()? -1 : 0;
}

void
Store::add(Span * ds)
{
  extend(n_disks + 1);
  disk[n_disks - 1] = ds;
}

void
Store::add(Store & s)
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
Store::free(Store & s)
{
  for (unsigned i = 0; i < s.n_disks; i++) {
    for (Span * sd = s.disk[i]; sd; sd = sd->link.next) {
      for (unsigned j = 0; j < n_disks; j++)
        for (Span * d = disk[j]; d; d = d->link.next)
          if (!strcmp(sd->pathname, d->pathname)) {
            if (sd->offset < d->offset)
              d->offset = sd->offset;
            d->blocks += sd->blocks;
            goto Lfound;
          }
      ink_release_assert(!"Store::free failed");
    Lfound:;
    }
  }
}

void
Store::sort()
{
  Span **vec = (Span **) alloca(sizeof(Span *) * n_disks);
  memset(vec, 0, sizeof(Span *) * n_disks);
  for (unsigned i = 0; i < n_disks; i++) {
    vec[i] = disk[i];
    disk[i] = NULL;
  }

  // sort by device

  unsigned n = 0;
  for (unsigned i = 0; i < n_disks; i++) {
    for (Span * sd = vec[i]; sd; sd = vec[i]) {
      vec[i] = vec[i]->link.next;
      for (unsigned d = 0; d < n; d++) {
        if (sd->disk_id == disk[d]->disk_id) {
          sd->link.next = disk[d];
          disk[d] = sd;
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
    Span * prev = 0;
    for (Span * sd = disk[i]; sd;) {
      Span *next = sd->link.next;
      if (next &&
          ((strcmp(sd->pathname, next->pathname) < 0) ||
           (!strcmp(sd->pathname, next->pathname) && sd->offset > next->offset))) {
        if (!prev) {
          disk[i] = next;
          sd->link.next = next->link.next;
          next->link.next = sd;
        } else {
          prev->link.next = next;
          sd->link.next = next->link.next;
          next->link.next = sd;
        }
        goto Lagain;
      }
      prev = sd;
      sd = next;
    }
  }

  // merge adjacent spans

  for (unsigned i = 0; i < n_disks; i++) {
    for (Span * sd = disk[i]; sd;) {
      Span *next = sd->link.next;
      if (next && !strcmp(sd->pathname, next->pathname)) {
        if (!sd->file_pathname) {
          sd->blocks += next->blocks;
        } else if (next->offset <= sd->end()) {
          if (next->end() >= sd->end())
            sd->blocks += (next->end() - sd->end());
        } else {
          sd = next;
          continue;
        }
        sd->link.next = next->link.next;
        delete next;
        sd = sd->link.next;
      } else
        sd = next;
    }
  }
}

int
Span::path(char *filename, int64_t * aoffset, char *buf, int buflen)
{
  ink_assert(!aoffset);
  Span *ds = this;

  if ((strlen(ds->pathname) + strlen(filename) + 2) > (size_t)buflen)
    return -1;
  if (!ds->file_pathname) {
    ink_filepath_make(buf, buflen, ds->pathname, filename);
  } else {
    ink_strlcpy(buf, ds->pathname, buflen);
  }

  return strlen(buf);
}

void
Store::delete_all()
{
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i])
      delete disk[i];
  }
  n_disks = 0;
  ats_free(disk);
  disk = NULL;
}

Store::~Store()
{
  delete_all();
}

Span::~Span()
{
  ats_free(pathname);
  if (link.next)
    delete link.next;
}

inline int
get_int64(int fd, int64_t & data)
{
  char buf[PATH_NAME_MAX + 1];
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
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
    Span *p = NULL;
    for (Span * sd = disk[i]; sd; sd = sd->link.next) {
      if (!strcmp(n, sd->pathname)) {
        found = true;
        if (p)
          p->link.next = sd->link.next;
        else
          disk[i] = sd->link.next;
        sd->link.next = NULL;
        delete sd;
        goto Lagain;
      }
      p = sd;
    }
  }
  return found ? 0 : -1;
}

const char *
Store::read_config(int fd)
{
  int n_dsstore = 0;
  int ln = 0;
  const char *err = NULL;
  Span *sd = NULL, *cur = NULL;
  Span *ns;

  // Get pathname if not checking file

  if (fd < 0) {
    xptr<char> storage_path(RecConfigReadConfigPath("proxy.config.cache.storage_filename", "storage.config"));

    Debug("cache_init", "Store::read_config, fd = -1, \"%s\"", (const char *)storage_path);
    fd =::open(storage_path, O_RDONLY);
    if (fd < 0) {
      err = "error on open";
      goto Lfail;
    }
  }
  // For each line

  char line[256];
  while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
    // update lines

    line[sizeof(line) - 1] = 0;
    ln++;

    // skip comments and blank lines

    if (*line == '#')
      continue;
    char *n = line;
    n += strspn(n, " \t\n");
    if (!*n)
      continue;

   int volume_id = getVolume(n);

    // parse
    Debug("cache_init", "Store::read_config: \"%s\"", n);

    char *e = strpbrk(n, " \t\n");
    int len = e ? e - n : strlen(n);
    (void) len;
    int64_t size = -1;
    while (e && *e && !ParseRules::is_digit(*e))
      e++;
    if (e && *e) {
      if ((size = ink_atoi64(e)) <= 0) {
        err = "error parsing size";
        goto Lfail;
      }
    }

    n[len] = 0;
    char *pp = Layout::get()->relative(n);
    ns = NEW(new Span);
    ns->vol_num = volume_id;
    Debug("cache_init", "Store::read_config - ns = NEW (new Span); ns->init(\"%s\",%" PRId64 "), ns->vol_num=%d",
      pp, size, ns->vol_num);
    if ((err = ns->init(pp, size))) {
      char buf[4096];
      snprintf(buf, sizeof(buf), "could not initialize storage \"%s\" [%s]", pp, err);
      REC_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, buf);
      Debug("cache_init", "Store::read_config - %s", buf);
      delete ns;
      ats_free(pp);
      continue;
    }
    ats_free(pp);
    n_dsstore++;

    // new Span
    {
      Span *prev = cur;
      cur = ns;
      if (!sd)
        sd = cur;
      else
        prev->link.next = cur;
    }
  }

  ::close(fd);
  // count the number of disks

  {
    extend(n_dsstore);
    cur = sd;
    Span *next = cur;
    int i = 0;
    while (cur) {
      next = cur->link.next;
      cur->link.next = NULL;
      disk[i++] = cur;
      cur = next;
    }
    sort();
  }

Lfail:;
  return NULL;
  return err;
}

#if TS_USE_INTERIM_CACHE == 1
const char *
Store::read_interim_config() {
  char p[PATH_NAME_MAX + 1];
  Span *sd = NULL;
  Span *ns;
  int interim_store = 0;
  REC_ReadConfigString(p, "proxy.config.cache.interim.storage", PATH_NAME_MAX);

  char *n = p;
  int sz = strlen(p);

  const char *err = NULL;
  for (int len = 0; n < p + sz; n += len + 1) {
    char *e = strpbrk(n, " \t\n");
    len = e ? e - n : strlen(n);
    n[len] = '\0';
    ns = NEW(new Span);
    if ((err = ns->init(n, -1))) {
      char buf[4096];
      snprintf(buf, sizeof(buf), "could not initialize storage \"%s\" [%s]", n,
          err);
      REC_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, buf);
      Debug("cache_init", "Store::read_interim_config - %s", buf);
      delete ns;
      continue;
    }
    ns->link.next = sd;
    sd = ns;
    interim_store++;
  }

  n_interim_disks = interim_store;
  interim_disk = (Span **) ats_malloc(interim_store * sizeof(Span *));
  {
    int i = 0;
    while (sd) {
      ns = sd;
      sd = sd->link.next;
      ns->link.next = NULL;
      interim_disk[i++] = ns;
    }
  }
  return NULL;
}
#endif

int
Store::write_config_data(int fd)
{
  for (unsigned i = 0; i < n_disks; i++)
    for (Span * sd = disk[i]; sd; sd = sd->link.next) {
      char buf[PATH_NAME_MAX + 64];
      snprintf(buf, sizeof(buf), "%s %" PRId64 "\n", sd->pathname, (int64_t) sd->blocks * (int64_t) STORE_BLOCK_SIZE);
      if (ink_file_fd_writestring(fd, buf) == -1)
        return (-1);
    }
  return 0;
}

#if defined(freebsd) || defined(darwin) || defined(openbsd)

const char *
Span::init(char *an, int64_t size)
{
  int devnum = 0;
  const char *err = NULL;
  int ret = 0;

  is_mmapable_internal = true;

  // handle symlinks

  char *n = NULL;
  int n_len = 0;
  char real_n[PATH_NAME_MAX];

  if ((n_len = readlink(an, real_n, sizeof(real_n) - 1)) > 0) {
    real_n[n_len] = 0;
    if (*real_n != '/') {
      char *rs = strrchr(an, '/');
      int l = 2;
      const char *ann = "./";

      if (rs) {
        ann = an;
        l = (rs - an) + 1;
      }
      memmove(real_n + l, real_n, strlen(real_n) + 1);
      memcpy(real_n, ann, l);
    }
    n = real_n;
  } else {
    n = an;
  }

  // stat the file system

  struct stat s;
  if ((ret = stat(n, &s)) < 0) {
    Warning("unable to stat '%s': %d %d, %s", n, ret, errno, strerror(errno));
    return "error stat of file";
  }

  int fd = socketManager.open(n, O_RDONLY);
  if (fd < 0) {
    Warning("unable to open '%s': %d, %s", n, fd, strerror(errno));
    return "unable to open";
  }

  struct statvfs fs;
  if ((ret = fstatvfs(fd, &fs)) < 0) {
    Warning("unable to statvfs '%s': %d %d, %s", n, ret, errno, strerror(errno));
    socketManager.close(fd);
    return "unable to statvfs";
  }

  hw_sector_size = fs.f_bsize;
  int64_t fsize = (int64_t) fs.f_blocks * (int64_t) fs.f_bsize;

  switch ((s.st_mode & S_IFMT)) {

  case S_IFBLK:{
  case S_IFCHR:
    // These IOCTLs are standard across the BSD family; Darwin has a different set though.
#if defined(DIOCGMEDIASIZE) && defined(DIOCGSECTORSIZE)
      if (ioctl(fd, DIOCGMEDIASIZE, &size) < 0) {
        Warning("unable to get disk information for '%s': %s", n, strerror(errno));
        err = "unable to get label information";
        goto Lfail;
      }
      if (ioctl(fd, DIOCGSECTORSIZE, &hw_sector_size) < 0) {
        Warning("unable to get disk information for '%s': %s", n, strerror(errno));
        err = "unable to get label information";
        goto Lfail;
      }
      devnum = s.st_rdev;
      break;
#else
      Warning("unable to get disk information for '%s': %s", n, strerror(errno));
      err = "no raw disk support on this platform";
      goto Lfail;
#endif
    }
  case S_IFDIR:
  case S_IFREG:
    if (size <= 0 || size > fsize) {
      Warning("bad or missing size for '%s': size %" PRId64 " fsize %" PRId64 "", n, (int64_t) size, fsize);
      err = "bad or missing size";
      goto Lfail;
    }
    devnum = s.st_dev;
    break;

  default:
    Warning("unknown file type '%s': %d", n, s.st_mode);
    return "unknown file type";
    break;
  }

  disk_id = devnum;

  pathname = ats_strdup(an);
  // igalic: blocks = size / hw_sector_size; was wrong TS-1707
  // This code needs refactoring to unify the code-paths which are equal across platforms.
  blocks = size / STORE_BLOCK_SIZE;
  file_pathname = !((s.st_mode & S_IFMT) == S_IFDIR);

  // This is so FreeBSD admins don't worry about our malicious code creating boot sector viruses:
  if (((s.st_mode & S_IFMT) == S_IFBLK) || ((s.st_mode & S_IFMT) == S_IFCHR)) {
    blocks--;
    offset = 1;
  }

  Debug("cache_init", "Span::init - %s hw_sector_size = %d  size = %" PRId64 ", blocks = %" PRId64 ", disk_id = %d, file_pathname = %d", pathname, hw_sector_size, size, blocks, disk_id, file_pathname);

Lfail:
  socketManager.close(fd);
  return err;
}

#endif

#if defined(solaris)

const char *
Span::init(char *filename, int64_t size)
{
  int devnum = 0;
  const char *err = NULL;
  int ret = 0;

  //
  // All file types on Solaris can be mmaped
  //
  is_mmapable_internal = true;

  int fd = socketManager.open(filename, O_RDONLY);
  if (fd < 0) {
    Warning("unable to open '%s': %d, %s", filename, fd, strerror(errno));
    return "unable to open";
  }

  // stat the file system
  struct stat s;
  if ((ret = fstat(fd, &s)) < 0) {
    Warning("unable to fstat '%s': %d %d, %s", filename, ret, errno, strerror(errno));
    err = "unable to fstat";
    goto Lfail;
  }


  switch ((s.st_mode & S_IFMT)) {

  case S_IFBLK:
  case S_IFCHR:
    devnum = s.st_rdev;
    // maybe we should use lseek(fd, 0, SEEK_END) here (it is portable)
    size = (int64_t) s.st_size;
    hw_sector_size = s.st_blksize;
    break;
  case S_IFDIR:
  case S_IFREG:
    int64_t fsize;
    struct statvfs fs;
    if ((ret = fstatvfs(fd, &fs)) < 0) {
      Warning("unable to statvfs '%s': %d %d, %s", filename, ret, errno, strerror(errno));
      err = "unable to statvfs";
      goto Lfail;
    }

    hw_sector_size = fs.f_bsize;
    fsize = (int64_t) fs.f_blocks * (int64_t) hw_sector_size;

    if (size <= 0 || size > fsize) {
      Warning("bad or missing size for '%s': size %" PRId64 " fsize %" PRId64 "", filename, (int64_t) size, fsize);
      err = "bad or missing size";
      goto Lfail;
    }

    devnum = s.st_dev;
    break;

  default:
    Warning("unknown file type '%s': %" PRId64 "", filename, (int64_t)(s.st_mode));
    err = "unknown file type";
    goto Lfail;
  }

  // estimate the disk SOLARIS specific
  if ((devnum >> 16) == 0x80) {
    disk_id = (devnum >> 3) & 0x3F;
  } else {
    disk_id = devnum;
  }

  pathname = ats_strdup(filename);
  // is this right Seems like this should be size / hw_sector_size
  // igalic: No. See TS-1707
  blocks = size / STORE_BLOCK_SIZE;
  file_pathname = !((s.st_mode & S_IFMT) == S_IFDIR);

  Debug("cache_init", "Span::init - %s hw_sector_size = %d  size = %" PRId64 ", blocks = %" PRId64 ", disk_id = %d, file_pathname = %d", filename, hw_sector_size, size, blocks, disk_id, file_pathname);

Lfail:
  socketManager.close(fd);
  return err;
}
#endif

#if defined(linux)
#include <unistd.h>             /* for close() */
#include <sys/ioctl.h>
#include <linux/hdreg.h>        /* for struct hd_geometry */
#include <linux/fs.h>           /* for BLKGETSIZE.  sys/mount.h is another candidate */


const char *
Span::init(char *filename, int64_t size)
{
  int devnum = 0, fd, arg = 0;
  int ret = 0, is_disk = 0;
  u_int64_t heads, sectors, cylinders, adjusted_sec;

  /* Fetch file type */
  struct stat stat_buf;
  Debug("cache_init", "Span::init(\"%s\",%" PRId64 ")", filename, size);
  if ((ret = stat(filename, &stat_buf)) < 0) {
    Warning("unable to stat '%s': %d %d, %s", filename, ret, errno, strerror(errno));
    return "cannot stat file";
  }
  switch (stat_buf.st_mode & S_IFMT) {
  case S_IFBLK:
  case S_IFCHR:
    devnum = stat_buf.st_rdev;
    Debug("cache_init", "Span::init - %s - devnum = %d",
          ((stat_buf.st_mode & S_IFMT) == S_IFBLK) ? "S_IFBLK" : "S_IFCHR", devnum);
    break;
  case S_IFDIR:
    devnum = stat_buf.st_dev;
    file_pathname = 0;
    Debug("cache_init", "Span::init - S_IFDIR - devnum = %d", devnum);
    break;
  case S_IFREG:
    devnum = stat_buf.st_dev;
    file_pathname = 1;
    size = stat_buf.st_size;
    Debug("cache_init", "Span::init - S_IFREG - devnum = %d", devnum);
    break;
  default:
    break;
  }

  if ((fd = socketManager.open(filename, O_RDONLY)) < 0) {
    Warning("unable to open '%s': %d, %s", filename, fd, strerror(errno));
    return "unable to open";
  }
  Debug("cache_init", "Span::init - socketManager.open(\"%s\", O_RDONLY) = %d", filename, fd);

  adjusted_sec = 1;
#ifdef BLKPBSZGET
  if (ioctl(fd, BLKPBSZGET, &arg) == 0)
#else
  if (ioctl(fd, BLKSSZGET, &arg) == 0)
#endif
  {
    hw_sector_size = arg;
    is_disk = 1;
    adjusted_sec = hw_sector_size / 512;
    Debug("cache_init", "Span::init - %s hw_sector_size=%d is_disk=%d adjusted_sec=%" PRId64,
          filename, hw_sector_size, is_disk, adjusted_sec);
  }

  alignment = 0;
#ifdef BLKALIGNOFF
  if (ioctl(fd, BLKALIGNOFF, &arg) == 0) {
    alignment = arg;
    Debug("cache_init", "Span::init - %s alignment = %d", filename, alignment);
  }
#endif

  if (is_disk) {
    u_int32_t ioctl_sectors = 0;
    u_int64_t ioctl_bytes = 0;
    u_int64_t physsectors = 0;

    /* Disks cannot be mmapped */
    is_mmapable_internal = false;

    if (!ioctl(fd, BLKGETSIZE64, &ioctl_bytes)) {
      heads = 1;
      cylinders = 1;
      physsectors = ioctl_bytes / hw_sector_size;
      sectors = physsectors;
    } else if (!ioctl(fd, BLKGETSIZE, &ioctl_sectors)) {
      heads = 1;
      cylinders = 1;
      physsectors = ioctl_sectors;
      sectors = physsectors / adjusted_sec;
    } else {
      struct hd_geometry geometry;
      if (!ioctl(fd, HDIO_GETGEO, &geometry)) {
        heads = geometry.heads;
        sectors = geometry.sectors;
        cylinders = geometry.cylinders;
        cylinders /= adjusted_sec;      /* do not round up */
      } else {
        /* Almost certainly something other than a disk device. */
        Warning("unable to get geometry '%s': %d %s", filename, errno, strerror(errno));
        return ("unable to get geometry");
      }
    }

    blocks = heads * sectors * cylinders;

    if (size > 0 && blocks * hw_sector_size != size) {
      Warning("Warning: you specified a size of %" PRId64 " for %s,\n", size, filename);
      Warning("but the device size is %" PRId64 ". Using minimum of the two.\n", (int64_t)blocks * (int64_t)hw_sector_size);
      if ((int64_t)blocks * (int64_t)hw_sector_size < size)
        size = (int64_t)blocks * (int64_t)hw_sector_size;
    } else {
      size = (int64_t)blocks * (int64_t)hw_sector_size;
    }

    /* I don't know why I'm redefining blocks to be something that is quite
     * possibly something other than the actual number of blocks, but the
     * code for other arches seems to.  Revisit this, perhaps. */
    // igalic: No. See TS-1707
    blocks = size / STORE_BLOCK_SIZE;

    Debug("cache_init", "Span::init physical sectors %" PRId64 " total size %" PRId64 " geometry size %" PRId64 " store blocks %" PRId64 "",
          physsectors, hw_sector_size * physsectors, size, blocks);

    pathname = ats_strdup(filename);
    file_pathname = 1;
  } else {
    Debug("cache_init", "Span::init - is_disk = %d, raw device = %s", is_disk, (major(devnum) == 162) ? "yes" : "no");
    if (major(devnum) == 162) {
      /* Oh, a raw device, how cute. */

      if (minor(devnum) == 0)
        return "The raw device control file (usually /dev/raw; major 162, minor 0) is not a valid cache location.\n";

      is_disk = 1;
      is_mmapable_internal = false;     /* I -think- */
      file_pathname = 1;
      pathname = ats_strdup(filename);
      isRaw = 1;

      if (size <= 0)
        return "When using raw devices for cache storage, you must specify a size\n";
    } else {
      /* Files can be mmapped */
      is_mmapable_internal = true;

      /* The code for other arches seems to want to dereference symlinks, but I
       * don't particularly understand that behaviour, so I'll just ignore it.
       * :) */

      pathname = ats_strdup(filename);
      if (!file_pathname)
        if (size <= 0)
          return "When using directories for cache storage, you must specify a size\n";
      Debug("cache_init", "Span::init - mapped file \"%s\", %" PRId64 "", pathname, size);
    }
    blocks = size / STORE_BLOCK_SIZE;
  }

  disk_id = devnum;

  socketManager.close(fd);

  return NULL;
}
#endif



void
Store::normalize()
{
  unsigned ndisks = 0;
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i])
      disk[ndisks++] = disk[i];
  }
  n_disks = ndisks;
}

static unsigned int
try_alloc(Store & target, Span * source, unsigned int start_blocks, bool one_only = false)
{
  unsigned int blocks = start_blocks;
  Span *ds = NULL;
  while (source && blocks) {
    if (source->blocks) {
      unsigned int a;           // allocated
      if (blocks > source->blocks)
        a = source->blocks;
      else
        a = blocks;
      Span *d = NEW(new Span(*source));

      d->pathname = ats_strdup(source->pathname);
      d->blocks = a;
      d->file_pathname = source->file_pathname;
      d->offset = source->offset;
      d->link.next = ds;

      if (d->file_pathname)
        source->offset += a;
      source->blocks -= a;
      ds = d;
      blocks -= a;
      if (one_only)
        break;
    }
    source = source->link.next;
  }
  if (ds)
    target.add(ds);
  return start_blocks - blocks;
}

void
Store::spread_alloc(Store & s, unsigned int blocks, bool mmapable)
{
  //
  // Count the eligable disks..
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

  for (unsigned i = 0; blocks && i < n_disks; i++) {
    if (!(mmapable && !disk[i]->is_mmapable())) {
      unsigned int target = blocks / disks_left;
      if (blocks - target > total_blocks(i + 1))
        target = blocks - total_blocks(i + 1);
      blocks -= try_alloc(s, disk[i], target);
      disks_left--;
    }
  }
}

void
Store::try_realloc(Store & s, Store & diff)
{
  for (unsigned i = 0; i < s.n_disks; i++) {
    Span *prev = 0;
    for (Span * sd = s.disk[i]; sd;) {
      for (unsigned j = 0; j < n_disks; j++)
        for (Span * d = disk[j]; d; d = d->link.next)
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
                Span *x = NEW(new Span(*d));
                x->pathname = ats_strdup(x->pathname);
                // d will be the first vol
                d->blocks = sd->offset - d->offset;
                d->link.next = x;
                // x will be the last vol
                x->offset = sd->offset + sd->blocks;
                x->blocks -= x->offset - d->offset;
                goto Lfound;
              }
            }
          }
      {
        if (!prev)
          s.disk[i] = s.disk[i]->link.next;
        else
          prev->link.next = sd->link.next;
        diff.extend(i + 1);
        sd->link.next = diff.disk[i];
        diff.disk[i] = sd;
        sd = prev ? prev->link.next : s.disk[i];
        continue;
      }
    Lfound:;
      prev = sd;
      sd = sd->link.next;
    }
  }
  normalize();
  s.normalize();
  diff.normalize();
}

//
// Stupid grab first availabled space allocator
//
void
Store::alloc(Store & s, unsigned int blocks, bool one_only, bool mmapable)
{
  unsigned int oblocks = blocks;
  for (unsigned i = 0; blocks && i < n_disks; i++) {
    if (!(mmapable && !disk[i]->is_mmapable())) {
      blocks -= try_alloc(s, disk[i], blocks, one_only);
      if (one_only && oblocks != blocks)
        break;
    }
  }
}

int
Span::write(int fd)
{
  char buf[32];

  if (ink_file_fd_writestring(fd, (char *) (pathname ? pathname : ")")) == -1)
    return (-1);
  if (ink_file_fd_writestring(fd, "\n") == -1)
    return (-1);

  snprintf(buf, sizeof(buf), "%" PRId64 "\n", blocks);
  if (ink_file_fd_writestring(fd, buf) == -1)
    return (-1);

  snprintf(buf, sizeof(buf), "%d\n", file_pathname);
  if (ink_file_fd_writestring(fd, buf) == -1)
    return (-1);

  snprintf(buf, sizeof(buf), "%" PRId64 "\n", offset);
  if (ink_file_fd_writestring(fd, buf) == -1)
    return (-1);

  snprintf(buf, sizeof(buf), "%d\n", (int) is_mmapable());
  if (ink_file_fd_writestring(fd, buf) == -1)
    return (-1);

  return 0;
}

int
Store::write(int fd, char *name)
{
  char buf[32];

  if (ink_file_fd_writestring(fd, name) == -1)
    return (-1);
  if (ink_file_fd_writestring(fd, "\n") == -1)
    return (-1);

  snprintf(buf, sizeof(buf), "%d\n", n_disks);
  if (ink_file_fd_writestring(fd, buf) == -1)
    return (-1);

  for (unsigned i = 0; i < n_disks; i++) {
    int n = 0;
    Span *sd = NULL;
    for (sd = disk[i]; sd; sd = sd->link.next) {
      n++;
    }

    snprintf(buf, sizeof(buf), "%d\n", n);
    if (ink_file_fd_writestring(fd, buf) == -1)
      return (-1);

    for (sd = disk[i]; sd; sd = sd->link.next) {
      if (sd->write(fd))
        return -1;
    }
  }
  return 0;
}

int
Span::read(int fd)
{
  char buf[PATH_NAME_MAX + 1], p[PATH_NAME_MAX + 1];

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
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
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d", &b) != 1)
    return (-1);
  file_pathname = (b ? true : false);

  if (get_int64(fd, offset) < 0) {
    return -1;
  }

  int tmp;
  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d", &tmp) != 1)
    return (-1);
  set_mmapable(tmp != 0);

  return (0);
}

int
Store::read(int fd, char *aname)
{
  char *name = aname;
  char tname[PATH_NAME_MAX + 1];
  char buf[PATH_NAME_MAX + 1];
  if (!aname)
    name = tname;

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%s\n", name) != 1)
    return (-1);

  if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
    return (-1);
  // the above line will guarantee buf to be no longer than PATH_NAME_MAX
  // so the next statement should be a safe use of sscanf
  // coverity[secure_coding]
  if (sscanf(buf, "%d\n", &n_disks) != 1)
    return (-1);

  disk = (Span **)ats_malloc(sizeof(Span *) * n_disks);
  if (!disk)
    return -1;
  memset(disk, 0, sizeof(Span *) * n_disks);
  for (unsigned i = 0; i < n_disks; i++) {
    int n = 0;

    if (ink_file_fd_readline(fd, PATH_NAME_MAX, buf) <= 0)
      return (-1);
    // the above line will guarantee buf to be no longer than PATH_NAME_MAX
    // so the next statement should be a safe use of sscanf
    // coverity[secure_coding]
    if (sscanf(buf, "%d\n", &n) != 1)
      return (-1);

    Span *sd = NULL;
    while (n--) {
      Span *last = sd;
      sd = NEW(new Span);

      if (!last)
        disk[i] = sd;
      else
        last->link.next = sd;
      if (sd->read(fd))
        goto Lbail;
    }
  }
  return 0;
Lbail:
  for (unsigned i = 0; i < n_disks; i++) {
    if (disk[i])
      delete disk[i];
  }
  return -1;
}

Span *
Span::dup()
{
  Span *ds = NEW(new Span(*this));
  ds->pathname = ats_strdup(pathname);
  if (ds->link.next)
    ds->link.next = ds->link.next->dup();
  return ds;
}

void
Store::dup(Store & s)
{
  s.n_disks = n_disks;
  s.disk = (Span **)ats_malloc(sizeof(Span *) * n_disks);
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
    for (int j = 0; j < disk[i]->paths(); j++) {
      char path[PATH_NAME_MAX + 1];
      Span *d = ds->nth(j);
      if (!clear_dirs && !d->file_pathname)
        continue;
      int r = d->path(filename, NULL, path, PATH_NAME_MAX);
      if (r < 0)
        return -1;
      int fd =::open(path, O_RDWR | O_CREAT, 0644);
      if (fd < 0)
        return -1;
      for (int b = 0; d->blocks; b++)
        if (socketManager.pwrite(fd, z, STORE_BLOCK_SIZE, d->offset + (b * STORE_BLOCK_SIZE)) < 0) {
          close(fd);
          return -1;
        }
      close(fd);
    }
  }
  return 0;
}
