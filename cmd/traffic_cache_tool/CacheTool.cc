/** @file

    Main program file for Cache Tool.

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

#include <iostream>
#include <list>
#include <memory>
#include <vector>
#include <map>
#include <getopt.h>
#include <system_error>
#include <cstring>
#include <fcntl.h>
#include <cctype>
#include <cstring>
#include <vector>
#include <unordered_set>
#include <ctime>
#include <bitset>
#include <cinttypes>

#include <ts/ink_memory.h>
#include <ts/ink_file.h>
#include <ts/BufferWriter.h>
#include <ts/CryptoHash.h>
#include <thread>

#include "File.h"
#include "CacheDefs.h"
#include "Command.h"
#include "CacheScan.h"

using ts::Bytes;
using ts::Megabytes;
using ts::CacheStoreBlocks;
using ts::CacheStripeBlocks;
using ts::StripeMeta;
using ts::CacheStripeDescriptor;
using ts::Errata;
using ts::FilePath;
using ts::CacheDirEntry;
using ts::MemSpan;
using ts::Doc;

enum { SILENT = 0, NORMAL, VERBOSE } Verbosity = NORMAL;
extern int cache_config_min_average_object_size;
extern CacheStoreBlocks Vol_hash_alloc_size;
extern int OPEN_RW_FLAG;
const Bytes ts::CacheSpan::OFFSET{CacheStoreBlocks{1}};
FilePath SpanFile;
FilePath VolumeFile;
ts::CommandTable Commands;

namespace ct
{
/* --------------------------------------------------------------------------------------- */
/// A live volume.
/// Volume data based on data from loaded spans.
struct Volume {
  int _idx;               ///< Volume index.
  CacheStoreBlocks _size; ///< Amount of storage allocated.
  std::vector<Stripe *> _stripes;

  /// Remove all data related to @a span.
  //  void clearSpan(Span *span);
  /// Remove all allocated space and stripes.
  void clear();
};

#if 0
void
Volume::clearSpan(Span* span)
{
  auto spot = std::remove_if(_stripes.begin(), _stripes.end(), [span,this](Stripe* stripe) { return stripe->_span == span ? ( this->_size -= stripe->_len , true ) : false; });
  _stripes.erase(spot, _stripes.end());
}
#endif

void
Volume::clear()
{
  _size.assign(0);
  _stripes.clear();
}
/* --------------------------------------------------------------------------------------- */
/// Data parsed from the volume config file.
struct VolumeConfig {
  Errata load(FilePath const &path);

  /// Data direct from the config file.
  struct Data {
    int _idx     = 0;         ///< Volume index.
    int _percent = 0;         ///< Size if specified as a percent.
    Megabytes _size{0};       ///< Size if specified as an absolute.
    CacheStripeBlocks _alloc; ///< Allocation size.

    // Methods handy for parsing
    bool
    hasSize() const
    {
      return _percent > 0 || _size > 0;
    }
    bool
    hasIndex() const
    {
      return _idx > 0;
    }
  };

  std::vector<Data> _volumes;
  typedef std::vector<Data>::iterator iterator;
  typedef std::vector<Data>::const_iterator const_iterator;

  iterator
  begin()
  {
    return _volumes.begin();
  }
  iterator
  end()
  {
    return _volumes.end();
  }
#if 0
  const_iterator
  begin() const
  {
    return _volumes.begin();
  }
  const_iterator
  end() const
  {
    return _volumes.end();
  }
#endif
  //  Errata validatePercentAllocation();
  void convertToAbsolute(ts::CacheStripeBlocks total_span_size);
};

#if 0
Errata
VolumeConfig::validatePercentAllocation()
{
  Errata zret;
  int n = 0;
  for (auto &vol : _volumes)
    n += vol._percent;
  if (n > 100)
    zret.push(0, 10, "Volume percent allocation ", n, " is more than 100%");
  return zret;
}
#endif

void
VolumeConfig::convertToAbsolute(ts::CacheStripeBlocks n)
{
  for (auto &vol : _volumes) {
    if (vol._percent) {
      vol._alloc.assign((n.count() * vol._percent + 99) / 100);
    } else {
      vol._alloc = round_up(vol._size);
    }
  }
}
/* --------------------------------------------------------------------------------------- */
struct Cache {
  ~Cache();

  Errata loadSpan(FilePath const &path);
  Errata loadSpanConfig(FilePath const &path);
  Errata loadSpanDirect(FilePath const &path, int vol_idx = -1, Bytes size = Bytes(-1));
  Errata loadURLs(FilePath const &path);

  Errata allocStripe(Span *span, int vol_idx, CacheStripeBlocks len);

  /// Change the @a span to have a single, unused stripe occupying the entire @a span.
  //  void clearSpan(Span *span);
  /// Clear all allocated space.
  void clearAllocation();

  enum class SpanDumpDepth { SPAN, STRIPE, DIRECTORY };
  void dumpSpans(SpanDumpDepth depth);
  void dumpVolumes();
  void build_stripe_hash_table();
  Stripe *key_to_stripe(CryptoHash *key, const char *hostname, int host_len);
  //  ts::CacheStripeBlocks calcTotalSpanPhysicalSize();
  ts::CacheStripeBlocks calcTotalSpanConfiguredSize();

  std::list<Span *> _spans;
  std::map<int, Volume> _volumes;
  std::vector<Stripe *> globalVec_stripe;
  std::unordered_set<ts::CacheURL *> URLset;
  unsigned short *stripes_hash_table;
};

Errata
Cache::allocStripe(Span *span, int vol_idx, CacheStripeBlocks len)
{
  auto rv = span->allocStripe(vol_idx, len);
  std::cout << span->_path << ":" << vol_idx << std::endl;
  if (rv.isOK()) {
    _volumes[vol_idx]._stripes.push_back(rv);
  }
  return rv.errata();
}

#if 0
void
Cache::clearSpan(Span* span)
{
  for ( auto& item : _volumes ) item.second.clearSpan(span);
  span->clear();
}
#endif

void
Cache::clearAllocation()
{
  for (auto span : _spans) {
    span->clear();
  }
  for (auto &item : _volumes) {
    item.second.clear();
  }
}
/* --------------------------------------------------------------------------------------- */
/// Temporary structure used for doing allocation computations.
class VolumeAllocator
{
  /// Working struct that tracks allocation information.
  struct V {
    VolumeConfig::Data const &_config; ///< Configuration instance.
    CacheStripeBlocks _size;           ///< Current actual size.
    int64_t _deficit;                  ///< fractional deficit
    int64_t _shares;                   ///< relative amount of free space to allocate

    V(VolumeConfig::Data const &config, CacheStripeBlocks size, int64_t deficit = 0, int64_t shares = 0)
      : _config(config), _size(size), _deficit(deficit), _shares(shares)
    {
    }
    V &
    operator=(V const &that)
    {
      new (this) V(that._config, that._size, that._deficit, that._shares);
      return *this;
    }
  };

  typedef std::vector<V> AV;
  AV _av; ///< Working vector of volume data.

  Cache _cache;       ///< Current state.
  VolumeConfig _vols; ///< Configuration state.

public:
  VolumeAllocator();

  Errata load(FilePath const &spanFile, FilePath const &volumeFile);
  Errata fillEmptySpans();
  Errata fillAllSpans();
  Errata allocateSpan(FilePath const &spanFile);
  void dumpVolumes();

protected:
  /// Update the allocation for a span.
  Errata allocateFor(Span &span);
};

VolumeAllocator::VolumeAllocator() {}

Errata
VolumeAllocator::load(FilePath const &spanFile, FilePath const &volumeFile)
{
  Errata zret;

  if (!volumeFile) {
    zret.push(0, 9, "Volume config file not set");
  }
  if (!spanFile) {
    zret.push(0, 9, "Span file not set");
  }

  if (zret) {
    zret = _vols.load(volumeFile);
    if (zret) {
      zret = _cache.loadSpan(spanFile);
      if (zret) {
        CacheStripeBlocks total = _cache.calcTotalSpanConfiguredSize();
        _vols.convertToAbsolute(total);
        for (auto &vol : _vols) {
          CacheStripeBlocks size(0);
          auto spot = _cache._volumes.find(vol._idx);
          if (spot != _cache._volumes.end()) {
            size = round_down(spot->second._size);
          }
          _av.push_back({vol, size, 0, 0});
        }
      }
    }
  }
  return zret;
}

void
VolumeAllocator::dumpVolumes()
{
  _cache.dumpVolumes();
}

Errata
VolumeAllocator::fillEmptySpans()
{
  Errata zret;
  // Walk the spans, skipping ones that are not empty.
  for (auto span : _cache._spans) {
    if (span->isEmpty()) {
      this->allocateFor(*span);
    }
  }
  return zret;
}

Errata
VolumeAllocator::allocateSpan(FilePath const &input_file_path)
{
  Errata zret;
  for (auto span : _cache._spans) {
    if (0 == strcmp(span->_path.path(), input_file_path.path())) {
      std::cout << "===============================" << std::endl;
      if (span->_header) {
        zret.push(0, 1, "Disk already initialized with valid header");
      } else {
        this->allocateFor(*span);
        span->updateHeader();
        for (auto &strp : span->_stripes) {
          strp->updateHeaderFooter();
        }
      }
    }
  }
  for (auto &_v : _av) {
    std::cout << _v._size << std::endl;
  }
  return zret;
}

Errata
VolumeAllocator::fillAllSpans()
{
  Errata zret;
  // clear all current volume allocations.
  for (auto &v : _av) {
    v._size.assign(0);
  }
  // Allocate for each span, clearing as it goes.
  _cache.clearAllocation();
  for (auto span : _cache._spans) {
    this->allocateFor(*span);
  }
  return zret;
}

Errata
VolumeAllocator::allocateFor(Span &span)
{
  Errata zret;

  /// Scaling factor for shares, effectively the accuracy.
  static const int64_t SCALE = 1000;
  int64_t total_shares       = 0;

  if (Verbosity >= NORMAL) {
    std::cout << "Allocating " << CacheStripeBlocks(round_down(span._len)).count() << " stripe blocks from span " << span._path
              << std::endl;
  }

  // Walk the volumes and get the relative allocations.
  for (auto &v : _av) {
    auto delta = v._config._alloc - v._size;
    if (delta > 0) {
      v._deficit = (delta.count() * SCALE) / v._config._alloc.count();
      v._shares  = delta.count() * v._deficit;
      total_shares += v._shares;
    } else {
      v._shares = 0;
    }
  }
  assert(total_shares != 0);
  // Now allocate blocks.
  CacheStripeBlocks span_blocks(round_down(span._free_space));
  CacheStripeBlocks span_used{0};

  // sort by deficit so least relatively full volumes go first.
  std::sort(_av.begin(), _av.end(), [](V const &lhs, V const &rhs) { return lhs._deficit > rhs._deficit; });
  for (auto &v : _av) {
    if (v._shares) {
      CacheStripeBlocks n{(((span_blocks - span_used).count() * v._shares) + total_shares - 1) / total_shares};
      CacheStripeBlocks delta{v._config._alloc - v._size};
      // Not sure why this is needed. But a large and empty volume can dominate the shares
      // enough to get more than it actually needs if the other volume are relative small or full.
      // I need to do more math to see if the weighting can be adjusted to not have this happen.
      n = std::min(n, delta);
      v._size += n;
      span_used += n;
      total_shares -= v._shares;
      Errata z = _cache.allocStripe(&span, v._config._idx, round_up(n));
      if (Verbosity >= NORMAL) {
        std::cout << "           " << n << " to volume " << v._config._idx << std::endl;
      }
      if (!z) {
        std::cout << z;
      }
    }
  }
  if (Verbosity >= NORMAL) {
    std::cout << "     Total " << span_used << std::endl;
  }
  if (OPEN_RW_FLAG) {
    if (Verbosity >= NORMAL) {
      std::cout << " Updating Header ... ";
    }
    zret = span.updateHeader();
  }
  _cache.dumpVolumes(); // debug
  if (Verbosity >= NORMAL) {
    if (zret) {
      std::cout << " Done" << std::endl;
    } else {
      std::cout << " Error" << std::endl << zret;
    }
  }

  return zret;
}
/* --------------------------------------------------------------------------------------- */
Errata
Cache::loadSpan(FilePath const &path)
{
  Errata zret;
  if (!path.has_path()) {
    zret = Errata::Message(0, EINVAL, "A span file specified by --span is required");
  } else if (!path.is_readable()) {
    zret = Errata::Message(0, EPERM, '\'', path.path(), "' is not readable.");
  } else if (path.is_regular_file()) {
    zret = this->loadSpanConfig(path);
  } else {
    zret = this->loadSpanDirect(path);
  }
  return zret;
}

Errata
Cache::loadSpanDirect(FilePath const &path, int vol_idx, Bytes size)
{
  Errata zret;
  std::unique_ptr<Span> span(new Span(path));
  zret = span->load();
  if (zret) {
    if (span->_header) {
      int nspb = span->_header->num_diskvol_blks;
      for (auto i = 0; i < nspb; ++i) {
        ts::CacheStripeDescriptor &raw = span->_header->stripes[i];
        Stripe *stripe                 = new Stripe(span.get(), raw.offset, raw.len);
        stripe->_idx                   = i;
        if (raw.free == 0) {
          stripe->_vol_idx = raw.vol_idx;
          stripe->_type    = raw.type;
          _volumes[stripe->_vol_idx]._stripes.push_back(stripe);
          _volumes[stripe->_vol_idx]._size += stripe->_len;
          stripe->vol_init_data();
        } else {
          span->_free_space += stripe->_len;
        }
        span->_stripes.push_back(stripe);
        globalVec_stripe.push_back(stripe);
      }
      span->_vol_idx = vol_idx;
    } else {
      span->clear();
    }
    _spans.push_back(span.release());
  }
  return zret;
}

Errata
Cache::loadSpanConfig(FilePath const &path)
{
  static const ts::TextView TAG_ID("id");
  static const ts::TextView TAG_VOL("volume");

  Errata zret;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();
    while (content) {
      ts::TextView line = content.take_prefix_at('\n');
      line.ltrim_if(&isspace);
      if (line.empty() || '#' == *line) {
        continue;
      }
      ts::TextView path = line.take_prefix_if(&isspace);
      if (path) {
        // After this the line is [size] [id=string] [volume=#]
        while (line) {
          ts::TextView value(line.take_prefix_if(&isspace));
          if (value) {
            ts::TextView tag(value.take_prefix_at('='));
            if (!tag) { // must be the size
            } else if (0 == strcasecmp(tag, TAG_ID)) {
            } else if (0 == strcasecmp(tag, TAG_VOL)) {
              ts::TextView text;
              auto n = ts::svtoi(value, &text);
              if (text == value && 0 < n && n < 256) {
              } else {
                zret.push(0, 0, "Invalid volume index '", value, "'");
              }
            }
          }
        }
        zret = this->loadSpan(FilePath(path));
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}

Errata
Cache::loadURLs(FilePath const &path)
{
  static const ts::TextView TAG_VOL("url");
  ts::URLparser parser;
  Errata zret;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();

    while (!content.empty()) {
      ts::TextView blob = content.take_prefix_at('\n');

      ts::TextView tag(blob.take_prefix_at('='));
      if (tag.empty()) {
      } else if (0 == strcasecmp(tag, TAG_VOL)) {
        std::string url;
        url.assign(blob.data(), blob.size());
        int port_ptr = -1, port_len = -1;
        int port = parser.getPort(url, port_ptr, port_len);
        if (port_ptr >= 0 && port_len > 0) {
          url.erase(port_ptr, port_len + 1); // get rid of :PORT
        }
        std::cout << "port # " << port << ":" << port_ptr << ":" << port_len << ":" << url << std::endl;
        ts::CacheURL *curl = new ts::CacheURL(url, port);
        URLset.emplace(curl);
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}

void
Cache::dumpSpans(SpanDumpDepth depth)
{
  if (depth >= SpanDumpDepth::SPAN) {
    for (auto span : _spans) {
      if (nullptr == span->_header) {
        std::cout << "Span: " << span->_path << " is uninitialized" << std::endl;
      } else {
        std::cout << "\n----------------------------------\n"
                  << "Span: " << span->_path << "\n----------------------------------\n"
                  << "#Magic: " << span->_header->magic << " #Volumes: " << span->_header->num_volumes
                  << "  #in use: " << span->_header->num_used << "  #free: " << span->_header->num_free
                  << "  #stripes: " << span->_header->num_diskvol_blks << "  Len(bytes): " << span->_header->num_blocks.value()
                  << std::endl;

        for (auto stripe : span->_stripes) {
          std::cout << "\n>>>>>>>>> Stripe " << static_cast<int>(stripe->_idx) << " @ " << stripe->_start
                    << " len=" << stripe->_len.count() << " blocks "
                    << " vol=" << static_cast<int>(stripe->_vol_idx) << " type=" << static_cast<int>(stripe->_type) << " "
                    << (stripe->isFree() ? "free" : "in-use") << std::endl;

          std::cout << "      " << stripe->_segments << " segments with " << stripe->_buckets << " buckets per segment for "
                    << stripe->_buckets * stripe->_segments * ts::ENTRIES_PER_BUCKET << " total directory entries taking "
                    << stripe->_buckets * stripe->_segments * sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET
                    //                        << " out of " << (delta-header_len).units() << " bytes."
                    << std::endl;
          if (depth >= SpanDumpDepth::STRIPE) {
            Errata r = stripe->loadMeta();
            if (r) {
              // print Meta[A][HEAD]
              std::string MetaCopy[2] = {"A", "B"};
              std::string MetaType[2] = {"HEAD", "FOOT"};
              for (int i = 0; i < 2; i++) {
                for (int j = 0; j < 2; j++) {
                  std::cout << "\n" << MetaCopy[i] << ":" << MetaType[j] << "\n" << std::endl;
                  std::cout << " Magic:" << stripe->_meta[i][j].magic
                            << "\n version: ink_major: " << stripe->_meta[i][j].version.ink_major
                            << "\n version: ink_minor: " << stripe->_meta[i][j].version.ink_minor
                            << "\n create_time: " << stripe->_meta[i][j].create_time
                            << "\n write_pos: " << stripe->_meta[i][j].write_pos
                            << "\n last_write_pos: " << stripe->_meta[i][j].last_write_pos
                            << "\n agg_pos: " << stripe->_meta[i][j].agg_pos << "\n generation: " << stripe->_meta[i][j].generation
                            << "\n phase: " << stripe->_meta[i][j].phase << "\n cycle: " << stripe->_meta[i][j].cycle
                            << "\n sync_serial: " << stripe->_meta[i][j].sync_serial
                            << "\n write_serial: " << stripe->_meta[i][j].write_serial << "\n dirty: " << stripe->_meta[i][j].dirty
                            << "\n sector_size: " << stripe->_meta[i][j].sector_size << std::endl;
                }
              }
              if (!stripe->validate_sync_serial()) {
                std::cout << "WARNING:::::Validity check failed for sync_serials" << std::endl;
              }
              stripe->_directory.clear();
            } else {
              std::cout << r;
            }
          }
        }
      }
    }
  }
}

void
Cache::dumpVolumes()
{
  for (auto const &elt : _volumes) {
    size_t size = 0;
    for (auto const &r : elt.second._stripes) {
      size += r->_len;
    }

    std::cout << "Volume " << elt.first << " has " << elt.second._stripes.size() << " stripes and " << size << " bytes"
              << std::endl;
  }
}

ts::CacheStripeBlocks
Cache::calcTotalSpanConfiguredSize()
{
  ts::CacheStripeBlocks zret(0);

  for (auto span : _spans) {
    zret += round_down(span->_len);
  }
  return zret;
}

#if 0
ts::CacheStripeBlocks
Cache::calcTotalSpanPhysicalSize()
{
  ts::CacheStripeBlocks zret(0);

  for (auto span : _spans) {
    // This is broken, physical_size doesn't work for devices, need to fix that.
    zret += round_down(span->_path.physical_size());
  }
  return zret;
}
#endif

Cache::~Cache()
{
  for (auto *span : _spans) {
    delete span;
  }
}

Errata
Span::load()
{
  Errata zret;
  if (!_path.is_readable()) {
    zret = Errata::Message(0, EPERM, _path, " is not readable.");
  } else if (_path.is_char_device() || _path.is_block_device()) {
    zret = this->loadDevice();
  } else if (_path.is_dir()) {
    zret.push(0, 1, "Directory support not yet available");
  } else {
    zret.push(0, EBADF, _path, " is not a valid file type");
  }
  return zret;
}

Errata
Span::loadDevice()
{
  Errata zret;
  int flags;

  flags = OPEN_RW_FLAG
#if defined(O_DIRECT)
          | O_DIRECT
#endif
#if defined(O_DSYNC)
          | O_DSYNC
#endif
    ;

  ats_scoped_fd fd{_path.open(flags)};

  if (fd != ts::NO_FD) {
    if (ink_file_get_geometry(fd, _geometry)) {
      off_t offset = ts::CacheSpan::OFFSET;
      CacheStoreBlocks span_hdr_size(1);                        // default.
      static const ssize_t BUFF_SIZE = CacheStoreBlocks::SCALE; // match default span_hdr_size
      alignas(512) char buff[BUFF_SIZE];
      ssize_t n = pread(fd, buff, BUFF_SIZE, offset);
      if (n >= BUFF_SIZE) {
        ts::SpanHeader &span_hdr = reinterpret_cast<ts::SpanHeader &>(buff);
        _base                    = round_up(offset);
        // See if it looks valid
        if (span_hdr.magic == ts::SpanHeader::MAGIC && span_hdr.num_diskvol_blks == span_hdr.num_used + span_hdr.num_free) {
          int nspb      = span_hdr.num_diskvol_blks;
          span_hdr_size = round_up(sizeof(ts::SpanHeader) + (nspb - 1) * sizeof(ts::CacheStripeDescriptor));
          _header.reset(new (malloc(span_hdr_size)) ts::SpanHeader);
          if (span_hdr_size <= BUFF_SIZE) {
            memcpy(static_cast<void *>(_header.get()), buff, span_hdr_size);
          } else {
            // TODO - check the pread return
            ssize_t n = pread(fd, _header.get(), span_hdr_size, offset);
            if (n < span_hdr_size) {
              std::cout << "Failed to read the Span Header" << std::endl;
            }
          }
          _len = _header->num_blocks;
        } else {
          std::cout << "Span: " << _path << " header is uninitialized or invalid" << std::endl;
          _len = round_down(_geometry.totalsz) - _base;
        }
        // valid FD means the device is accessible and has enough storage to be configured.
        _fd     = fd.release();
        _offset = _base + span_hdr_size;
      } else {
        zret = Errata::Message(0, errno, "Failed to read from ", _path, '[', errno, ':', strerror(errno), ']');
      }
    } else {
      zret = Errata::Message(0, 23, "Unable to get device geometry for ", _path);
    }
  } else {
    zret = Errata::Message(0, errno, "Unable to open ", _path);
  }
  return zret;
}

ts::Rv<Stripe *>
Span::allocStripe(int vol_idx, CacheStripeBlocks len)
{
  for (auto spot = _stripes.begin(), limit = _stripes.end(); spot != limit; ++spot) {
    Stripe *stripe = *spot;
    if (stripe->isFree()) {
      if (len < stripe->_len) {
        // If the remains would be less than a stripe block, just take it all.
        if (stripe->_len <= (len + CacheStripeBlocks(1))) {
          stripe->_vol_idx = vol_idx;
          stripe->_type    = 1;
          return stripe;
        } else {
          Stripe *ns = new Stripe(this, stripe->_start, len);
          stripe->_start += len;
          stripe->_len -= len;
          ns->_vol_idx = vol_idx;
          ns->_type    = 1;
          _stripes.insert(spot, ns);
          return ns;
        }
      }
    }
  }
  return ts::Rv<Stripe *>(nullptr,
                          Errata::Message(0, 15, "Failed to allocate stripe of size ", len, " - no free block large enough"));
}

bool
Span::isEmpty() const
{
  return std::all_of(_stripes.begin(), _stripes.end(), [](Stripe *s) { return s->_vol_idx == 0; });
}

Errata
Span::clear()
{
  Stripe *stripe;
  std::for_each(_stripes.begin(), _stripes.end(), [](Stripe *s) { delete s; });
  _stripes.clear();

  // Gah, due to lack of anything better, TS depends on the number of usable blocks to be consistent
  // with internal calculations so have to match that here. Yay.
  CacheStoreBlocks eff = _len - _base; // starting # of usable blocks.
  // The maximum number of volumes that can store stored, accounting for the space used to store the descriptors.
  int n   = (eff - sizeof(ts::SpanHeader)) / (CacheStripeBlocks::SCALE + sizeof(CacheStripeDescriptor));
  _offset = _base + round_up(sizeof(ts::SpanHeader) + (n - 1) * sizeof(CacheStripeDescriptor));
  stripe  = new Stripe(this, _offset, _len - _offset);
  stripe->vol_init_data();
  stripe->InitializeMeta();
  _stripes.push_back(stripe);
  _free_space = stripe->_len;

  return Errata();
}

Errata
Span::updateHeader()
{
  Errata zret;
  int n = _stripes.size();
  CacheStripeDescriptor *sd;
  CacheStoreBlocks hdr_size = round_up(sizeof(ts::SpanHeader) + (n - 1) * sizeof(ts::CacheStripeDescriptor));
  void *raw                 = ats_memalign(512, hdr_size);
  ts::SpanHeader *hdr       = static_cast<ts::SpanHeader *>(raw);
  std::bitset<ts::MAX_VOLUME_IDX + 1> volume_mask;

  hdr->magic            = ts::SpanHeader::MAGIC;
  hdr->num_free         = 0;
  hdr->num_used         = 0;
  hdr->num_diskvol_blks = n;
  hdr->num_blocks       = _len;

  sd = hdr->stripes;
  for (auto stripe : _stripes) {
    sd->offset               = stripe->_start;
    sd->len                  = stripe->_len;
    sd->vol_idx              = stripe->_vol_idx;
    sd->type                 = stripe->_type;
    volume_mask[sd->vol_idx] = true;
    if (sd->vol_idx == 0) {
      sd->free = true;
      ++(hdr->num_free);
    } else {
      sd->free = false;
      ++(hdr->num_used);
    }

    ++sd;
  }
  volume_mask[0]   = false; // don't include free stripes in distinct volume count.
  hdr->num_volumes = volume_mask.count();
  _header.reset(hdr);
  if (OPEN_RW_FLAG) {
    ssize_t r = pwrite(_fd, hdr, hdr_size, ts::CacheSpan::OFFSET);
    if (r < ts::CacheSpan::OFFSET) {
      zret.push(0, errno, "Failed to update span - ", strerror(errno));
    }
  } else {
    std::cout << "Writing not enabled, no updates perfomed" << std::endl;
  }
  return zret;
}

void
Span::clearPermanently()
{
  if (OPEN_RW_FLAG) {
    alignas(512) static char zero[CacheStoreBlocks::SCALE]; // should be all zero, it's static.
    std::cout << "Clearing " << _path << " permanently on disk ";
    ssize_t n = pwrite(_fd, zero, sizeof(zero), ts::CacheSpan::OFFSET);
    if (n == sizeof(zero)) {
      std::cout << "done";
    } else {
      const char *text = strerror(errno);
      std::cout << "failed";
      if (n >= 0) {
        std::cout << " - " << n << " of " << sizeof(zero) << " bytes written";
      }
      std::cout << " - " << text;
    }

    std::cout << std::endl;
    // clear the stripes as well
    for (auto *strp : _stripes) {
      strp->loadMeta();
      std::cout << "Clearing stripe @" << strp->_start << " of length: " << strp->_len << std::endl;
      strp->clear();
    }
  } else {
    std::cout << "Clearing " << _path << " not performed, write not enabled" << std::endl;
  }
}

// explicit pair for random table in build_vol_hash_table
struct rtable_pair {
  unsigned int rval; ///< relative value, used to sort.
  unsigned int idx;  ///< volume mapping table index.
};

// comparison operator for random table in build_vol_hash_table
// sorts based on the randomly assigned rval
static int
cmprtable(const void *aa, const void *bb)
{
  rtable_pair *a = (rtable_pair *)aa;
  rtable_pair *b = (rtable_pair *)bb;
  if (a->rval < b->rval) {
    return -1;
  }
  if (a->rval > b->rval) {
    return 1;
  }
  return 0;
}

unsigned int
next_rand(unsigned int *p)
{
  unsigned int seed = *p;
  seed              = 1103515145 * seed + 12345;
  *p                = seed;
  return seed;
}

void
Cache::build_stripe_hash_table()
{
  int num_stripes = globalVec_stripe.size();
  CacheStoreBlocks total;
  unsigned int *forvol         = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int *gotvol         = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int *rnd            = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned short *ttable       = (unsigned short *)ats_malloc(sizeof(unsigned short) * VOL_HASH_TABLE_SIZE);
  unsigned int *rtable_entries = (unsigned int *)ats_malloc(sizeof(unsigned int) * num_stripes);
  unsigned int rtable_size     = 0;
  int i                        = 0;
  uint64_t used                = 0;

  // estimate allocation
  for (auto &elt : globalVec_stripe) {
    // printf("stripe length %" PRId64 "\n", elt->_len.count());
    rtable_entries[i] = static_cast<int64_t>(elt->_len) / Vol_hash_alloc_size;
    rtable_size += rtable_entries[i];
    uint64_t x = elt->hash_id.fold();
    // seed random number generator
    rnd[i] = (unsigned int)x;
    total += elt->_len;
    i++;
  }
  i = 0;
  for (auto &elt : globalVec_stripe) {
    forvol[i] = total ? static_cast<int64_t>(VOL_HASH_TABLE_SIZE * elt->_len) / total : 0;
    used += forvol[i];
    gotvol[i] = 0;
    i++;
  }

  // spread around the excess
  int extra = VOL_HASH_TABLE_SIZE - used;
  for (int i = 0; i < extra; i++) {
    forvol[i % num_stripes]++;
  }

  // initialize table to "empty"
  for (int i = 0; i < VOL_HASH_TABLE_SIZE; i++) {
    ttable[i] = VOL_HASH_EMPTY;
  }

  // generate random numbers proportaion to allocation
  rtable_pair *rtable = (rtable_pair *)ats_malloc(sizeof(rtable_pair) * rtable_size);
  int rindex          = 0;
  for (int i = 0; i < num_stripes; i++) {
    for (int j = 0; j < (int)rtable_entries[i]; j++) {
      rtable[rindex].rval = next_rand(&rnd[i]);
      rtable[rindex].idx  = i;
      rindex++;
    }
  }
  assert(rindex == (int)rtable_size);
  // sort (rand #, vol $ pairs)
  qsort(rtable, rtable_size, sizeof(rtable_pair), cmprtable);
  unsigned int width = (1LL << 32) / VOL_HASH_TABLE_SIZE;
  unsigned int pos; // target position to allocate
  // select vol with closest random number for each bucket
  i = 0; // index moving through the random numbers
  for (int j = 0; j < VOL_HASH_TABLE_SIZE; j++) {
    pos = width / 2 + j * width; // position to select closest to
    while (pos > rtable[i].rval && i < (int)rtable_size - 1) {
      i++;
    }
    ttable[j] = rtable[i].idx;
    gotvol[rtable[i].idx]++;
  }
  for (int i = 0; i < num_stripes; i++) {
    printf("build_vol_hash_table index %d mapped to %d requested %d got %d\n", i, i, forvol[i], gotvol[i]);
  }
  stripes_hash_table = ttable;

  ats_free(forvol);
  ats_free(gotvol);
  ats_free(rnd);
  ats_free(rtable_entries);
  ats_free(rtable);
}

Stripe *
Cache::key_to_stripe(CryptoHash *key, const char *hostname, int host_len)
{
  uint32_t h = (key->slice32(2) >> DIR_TAG_WIDTH) % VOL_HASH_TABLE_SIZE;
  return globalVec_stripe[stripes_hash_table[h]];
}

/* --------------------------------------------------------------------------------------- */
Errata
VolumeConfig::load(FilePath const &path)
{
  static const ts::TextView TAG_SIZE("size");
  static const ts::TextView TAG_VOL("volume");

  Errata zret;

  int ln = 0;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::TextView content = cfile.content();
    while (content) {
      Data v;

      ++ln;
      ts::TextView line = content.take_prefix_at('\n');
      line.ltrim_if(&isspace);
      if (line.empty() || '#' == *line) {
        continue;
      }

      while (line) {
        ts::TextView value(line.take_prefix_if(&isspace));
        ts::TextView tag(value.take_prefix_at('='));
        if (tag.empty()) {
          zret.push(0, 1, "Line ", ln, " is invalid");
        } else if (0 == strcasecmp(tag, TAG_SIZE)) {
          if (v.hasSize()) {
            zret.push(0, 5, "Line ", ln, " has field ", TAG_SIZE, " more than once");
          } else {
            ts::TextView text;
            auto n = ts::svtoi(value, &text);
            if (text) {
              ts::TextView percent(text.data_end(), value.data_end()); // clip parsed number.
              if (percent.empty()) {
                v._size = CacheStripeBlocks(round_up(Megabytes(n)));
                if (v._size.count() != n) {
                  zret.push(0, 0, "Line ", ln, " size ", n, " was rounded up to ", v._size);
                }
              } else if ('%' == *percent && percent.size() == 1) {
                v._percent = n;
              } else {
                zret.push(0, 3, "Line ", ln, " has invalid value '", value, "' for ", TAG_SIZE, " field");
              }
            } else {
              zret.push(0, 2, "Line ", ln, " has invalid value '", value, "' for ", TAG_SIZE, " field");
            }
          }
        } else if (0 == strcasecmp(tag, TAG_VOL)) {
          if (v.hasIndex()) {
            zret.push(0, 6, "Line ", ln, " has field ", TAG_VOL, " more than once");
          } else {
            ts::TextView text;
            auto n = ts::svtoi(value, &text);
            if (text == value) {
              v._idx = n;
            } else {
              zret.push(0, 4, "Line ", ln, " has invalid value '", value, "' for ", TAG_VOL, " field");
            }
          }
        }
      }
      if (v.hasSize() && v.hasIndex()) {
        _volumes.push_back(std::move(v));
      } else {
        if (!v.hasSize()) {
          zret.push(0, 7, "Line ", ln, " does not have the required field ", TAG_SIZE);
        }
        if (!v.hasIndex()) {
          zret.push(0, 8, "Line ", ln, " does not have the required field ", TAG_VOL);
        }
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}
/* --------------------------------------------------------------------------------------- */
struct option Options[] = {{"help", 0, nullptr, 'h'},  {"spans", 1, nullptr, 's'}, {"volumes", 1, nullptr, 'v'},
                           {"write", 0, nullptr, 'w'}, {"input", 1, nullptr, 'i'}, {"device", 1, nullptr, 'd'},
                           {"aos", 1, nullptr, 'o'},   {nullptr, 0, nullptr, 0}};
} // namespace ct

using namespace ct;
Errata
List_Stripes(Cache::SpanDumpDepth depth)
{
  Errata zret;
  Cache cache;

  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(depth);
    cache.dumpVolumes();
  }
  return zret;
}

Errata
Cmd_Allocate_Empty_Spans(int argc, char *argv[])
{
  Errata zret;
  VolumeAllocator va;

  //  OPEN_RW_FLAG = O_RDWR;
  zret = va.load(SpanFile, VolumeFile);
  if (zret) {
    va.fillEmptySpans();
  }

  return zret;
}

Errata
Simulate_Span_Allocation(int argc, char *argv[])
{
  Errata zret;
  VolumeAllocator va;

  if (!VolumeFile) {
    zret.push(0, 9, "Volume config file not set");
  }
  if (!SpanFile) {
    zret.push(0, 9, "Span file not set");
  }

  if (zret) {
    if ((zret = va.load(SpanFile, VolumeFile)).isOK()) {
      zret = va.fillAllSpans();
      va.dumpVolumes();
    }
  }
  return zret;
}

Errata
Clear_Spans()
{
  Errata zret;
  Cache cache;

  if (!OPEN_RW_FLAG) {
    zret.push(0, 1, "Writing Not Enabled.. Please use --write to enable writing to disk");
    return zret;
  }

  if ((zret = cache.loadSpan(SpanFile))) {
    for (auto *span : cache._spans) {
      span->clearPermanently();
    }
  }

  return zret;
}

Errata
Find_Stripe(FilePath const &input_file_path)
{
  // scheme=http user=u password=p host=172.28.56.109 path=somepath query=somequery port=1234
  // input file format: scheme://hostname:port/somepath;params?somequery user=USER password=PASS
  // user, password, are optional; scheme and host are required

  //  char* h= http://user:pass@IPADDRESS/path_to_file;?port      <== this is the format we need
  //  url_matcher matcher;
  //  if (matcher.match(h))
  //    std::cout << h << " : is valid" << std::endl;
  //  else
  //    std::cout << h << " : is NOT valid" << std::endl;

  Errata zret;
  Cache cache;
  if (input_file_path) {
    printf("passed argv %s\n", input_file_path.path());
  }
  cache.loadURLs(input_file_path);
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    cache.build_stripe_hash_table();
    for (auto host : cache.URLset) {
      CryptoContext ctx;
      CryptoHash hashT;
      ts::LocalBufferWriter<33> w;
      ctx.update(host->url.data(), host->url.size());
      ctx.update(&host->port, sizeof(host->port));
      ctx.finalize(hashT);
      Stripe *stripe_ = cache.key_to_stripe(&hashT, host->url.data(), host->url.size());
      w.print("{}", hashT);
      printf("hash of %.*s is %.*s: Stripe  %s \n", (int)host->url.size(), host->url.data(), static_cast<int>(w.size()), w.data(),
             stripe_->hashText.data());
    }
  }

  return zret;
}

Errata
dir_check()
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto &stripe : cache.globalVec_stripe) {
      stripe->dir_check();
    }
  }
  printf("\nCHECK succeeded\n");
  return zret;
}

Errata
walk_bucket_chain(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        for (auto strp : sp->_stripes) {
          strp->loadMeta();
          strp->loadDir();
          strp->walk_all_buckets();
        }
      }
    }
  }
  return zret;
}

Errata
Clear_Span(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        printf("clearing %s\n", devicePath.data());
        sp->clearPermanently();
      }
    }
  }
  return zret;
}

Errata
Check_Freelist(std::string devicePath)
{
  Errata zret;
  Cache cache;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      if (devicePath.size() > 0 && 0 == strncmp(sp->_path.path(), devicePath.data(), devicePath.size())) {
        printf("Scanning %s\n", devicePath.data());
        for (auto strp : sp->_stripes) {
          strp->loadMeta();
          strp->loadDir();
          for (int s = 0; s < strp->_segments; s++) {
            strp->check_loop(s);
          }
        }
        break;
      }
    }
  }
  return zret;
}

Errata
Init_disk(FilePath const &input_file_path)
{
  Errata zret;
  Cache cache;
  VolumeAllocator va;

  if (!OPEN_RW_FLAG) {
    zret.push(0, 1, "Writing Not Enabled.. Please use --write to enable writing to disk");
    return zret;
  }

  zret = va.load(SpanFile, VolumeFile);
  va.allocateSpan(input_file_path);

  return zret;
}

Errata
Get_Response(FilePath const &input_file_path)
{
  // scheme=http user=u password=p host=172.28.56.109 path=somepath query=somequery port=1234
  // input file format: scheme://hostname:port/somepath;params?somequery user=USER password=PASS
  // user, password, are optional; scheme and host are required

  //  char* h= http://user:pass@IPADDRESS/path_to_file;?port      <== this is the format we need

  Errata zret;
  Cache cache;
  if (input_file_path) {
    printf("passed argv %s\n", input_file_path.path());
  }
  cache.loadURLs(input_file_path);
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    cache.build_stripe_hash_table();
    for (auto host : cache.URLset) {
      CryptoContext ctx;
      CryptoHash hashT;
      ts::LocalBufferWriter<33> w;
      ctx.update(host->url.data(), host->url.size());
      ctx.update(&host->port, sizeof(host->port));
      ctx.finalize(hashT);
      Stripe *stripe_ = cache.key_to_stripe(&hashT, host->url.data(), host->url.size());
      w.print("{}", hashT);
      printf("hash of %.*s is %.*s: Stripe  %s \n", (int)host->url.size(), host->url.data(), static_cast<int>(w.size()), w.data(),
             stripe_->hashText.data());
      CacheDirEntry *dir_result = nullptr;
      stripe_->loadMeta();
      stripe_->loadDir();
      stripe_->dir_probe(&hashT, dir_result, nullptr);
    }
  }

  return zret;
}

void static scan_span(Span *span)
{
  for (auto strp : span->_stripes) {
    strp->loadMeta();
    strp->loadDir();
    strp->walk_all_buckets();
    CacheScan cs(strp);
    cs.Scan();

    // break; // to be removed
  }
}

Errata
Scan_Cache()
{
  Errata zret;
  Cache cache;
  std::vector<std::thread> threadPool;
  if ((zret = cache.loadSpan(SpanFile))) {
    cache.dumpSpans(Cache::SpanDumpDepth::SPAN);
    for (auto sp : cache._spans) {
      threadPool.emplace_back(scan_span, sp); // move constructor is necessary since std::thread is non copyable
      // break; // to be removed
    }
    for (auto &th : threadPool)
      th.join();
  }
  return zret;
}

int
main(int argc, char *argv[])
{
  int opt_idx = 0;
  int opt_val;
  bool help = false;
  FilePath input_url_file;
  std::string inputFile;
  char *inp = nullptr;
  while (-1 != (opt_val = getopt_long(argc, argv, "h", Options, &opt_idx))) {
    switch (opt_val) {
    case 'h':
      printf("Usage: %s --span <SPAN> --volume <FILE> <COMMAND> [<SUBCOMMAND> ...]\n", argv[0]);
      help = true;
      break;
    case 's':
      SpanFile = optarg;
      break;
    case 'v':
      VolumeFile = optarg;
      break;
    case 'w':
      OPEN_RW_FLAG = O_RDWR;
      std::cout << "NOTE: Writing to physical devices enabled" << std::endl;
      break;
    case 'i':
      input_url_file = optarg;
      break;
    case 'o':
      cache_config_min_average_object_size = atoi(optarg);
      break;
    case 'd':
      if (!inp) {
        inp = strdup(optarg);
        inputFile.assign(inp, strlen(inp));
      }
      break;
    }
  }

  Commands.add("list", "List elements of the cache", []() { return List_Stripes(Cache::SpanDumpDepth::SPAN); })
    .subCommand(std::string("stripes"), std::string("List the stripes"),
                []() { return List_Stripes(Cache::SpanDumpDepth::STRIPE); });
  Commands.add(std::string("clear"), std::string("Clear spans"), []() { return Clear_Spans(); })
    .subCommand(std::string("span"), std::string("clear an specific span"),
                [&](int, char *argv[]) { return Clear_Span(inputFile); });

  auto &c = Commands.add(std::string("dir_check"), std::string("cache check"));
  c.subCommand(std::string("full"), std::string("Full report of the cache storage"), &dir_check);
  c.subCommand(std::string("freelist"), std::string("check the freelist for loop"),
               [&](int, char *argv[]) { return Check_Freelist(inputFile); });
  c.subCommand(std::string("bucket_chain"), std::string("walk bucket chains for loops"),
               [&](int, char *argv[]) { return walk_bucket_chain(inputFile); });
  Commands.add(std::string("volumes"), std::string("Volumes"), &Simulate_Span_Allocation);
  Commands.add(std::string("alloc"), std::string("Storage allocation"))
    .subCommand(std::string("free"), std::string("Allocate storage on free (empty) spans"), &Cmd_Allocate_Empty_Spans);
  Commands.add(std::string("find"), std::string("Find Stripe Assignment"),
               [&](int, char *argv[]) { return Find_Stripe(input_url_file); });
  Commands.add(std::string("clearspan"), std::string("clear specific span"))
    .subCommand(std::string("span"), std::string("device path"), [&](int, char *argv[]) { return Clear_Span(inputFile); });
  Commands.add(std::string("retrieve"), std::string(" retrieve the response of the given list of URLs"),
               [&](int, char *argv[]) { return Get_Response(input_url_file); });
  Commands.add(std::string("init"), std::string(" Initializes uninitialized span"),
               [&](int, char *argv[]) { return Init_disk(input_url_file); });
  Commands.add(std::string("scan"), std::string(" Scans the whole cache and lists the urls of the cached contents"),
               [&](int, char *argv[]) { return Scan_Cache(); });
  Commands.setArgIndex(optind);

  if (help) {
    Commands.helpMessage(argc - 1, argv + 1);
    exit(1);
  }

  Errata result = Commands.invoke(argc, argv);

  if (result.size()) {
    std::cerr << result;
    exit(1);
  }
  if (inp) {
    free(inp);
  }
  return 0;
}
