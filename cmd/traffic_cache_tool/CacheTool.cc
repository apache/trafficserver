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
#include <ts/ink_memory.h>
#include <ts/ink_file.h>
#include <ts/MemView.h>
#include <getopt.h>
#include <system_error>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include "File.h"
#include "CacheDefs.h"
#include "Command.h"

using ts::Bytes;
using ts::Megabytes;
using ts::CacheStoreBlocks;
using ts::CacheStripeBlocks;
using ts::StripeMeta;
using ts::CacheStripeDescriptor;
using ts::Errata;
using ts::FilePath;
using ts::MemView;
using ts::CacheDirEntry;

const Bytes ts::CacheSpan::OFFSET{CacheStoreBlocks{1}};

namespace
{
FilePath SpanFile;
FilePath VolumeFile;

ts::CommandTable Commands;

// Default this to read only, only enable write if specifically required.
int OPEN_RW_FLAG = O_RDONLY;

struct Stripe;

struct Span {
  Span(FilePath const &path) : _path(path) {}
  Errata load();
  Errata loadDevice();

  /// No allocated stripes on this span.
  bool isEmpty() const;

  /// Replace all existing stripes with a single unallocated stripe covering the span.
  Errata clear();

  /// This is broken and needs to be cleaned up.
  void clearPermanently();

  ts::Rv<Stripe *> allocStripe(int vol_idx, CacheStripeBlocks len);
  Errata updateHeader(); ///< Update serialized header and write to disk.

  FilePath _path;           ///< File system location of span.
  ats_scoped_fd _fd;        ///< Open file descriptor for span.
  int _vol_idx = 0;         ///< Forced volume.
  CacheStoreBlocks _base;   ///< Offset to first usable byte.
  CacheStoreBlocks _offset; ///< Offset to first content byte.
  // The space between _base and _offset is where the span information is stored.
  CacheStoreBlocks _len;         ///< Total length of span.
  CacheStoreBlocks _free_space;  ///< Total size of free stripes.
  ink_device_geometry _geometry; ///< Geometry of span.
  /// Local copy of serialized header data stored on in the span.
  std::unique_ptr<ts::SpanHeader> _header;
  /// Live information about stripes.
  /// Seeded from @a _header and potentially agumented with direct probing.
  std::list<Stripe *> _stripes;
};
/* --------------------------------------------------------------------------------------- */
struct Stripe {
  /// Meta data is stored in 4 copies A/B and Header/Footer.
  enum Copy { A = 0, B = 1 };
  enum { HEAD = 0, FOOT = 1 };

  /// Piece wise memory storage for the directory.
  struct Chunk {
    Bytes _start = 0; ///< Starting offset relative to physical device of span.
    Bytes _skip  = 0; ///< # of bytes not valid at the start of the first block.
    Bytes _clip  = 0; ///< # of bytes not valid at the end of the last block.

    typedef std::vector<MemView> Chain;
    Chain _chain; ///< Chain of blocks.

    ~Chunk();

    void append(MemView m);
    void clear();
  };

  /// Hold a list of chunks representing an extended piece of memory.
  typedef std::vector<Chunk> Memory;

  /// Construct from span header data.
  Stripe(Span *span, Bytes start, CacheStoreBlocks len);

  /// Is stripe unallocated?
  bool isFree() const;

  /** Probe a chunk of memory @a mem for stripe metadata.

      @a mem is updated to remove memory that has been probed. If @a
      meta is not @c nullptr then it is used for additional cross
      checking.

      @return @c true if @a mem has valid data, @c false otherwise.
  */
  bool probeMeta(MemView &mem, StripeMeta const* meta = nullptr);

  /// Check a buffer for being valid stripe metadata.
  /// @return @c true if valid, @c false otherwise.
  static bool validateMeta(StripeMeta const *meta);

  /// Load metadata for this stripe.
  Errata loadMeta();

  /// Initialize the live data from the loaded serialized data.
  void updateLiveData(enum Copy c);

  Span *_span;           ///< Hosting span.
  Bytes _start;          ///< Offset of first byte of stripe.
  Bytes _content;        ///< Start of content.
  CacheStoreBlocks _len; ///< Length of stripe.
  uint8_t _vol_idx = 0;  ///< Volume index.
  uint8_t _type    = 0;  ///< Stripe type.
  uint8_t _idx     = -1; ///< Stripe index in span.

  int64_t _buckets;  ///< Number of buckets per segment.
  int64_t _segments; ///< Number of segments.

  /// Meta copies, indexed by A/B then HEAD/FOOT.
  StripeMeta _meta[2][2];
  /// Locations for the meta data.
  CacheStoreBlocks _meta_pos[2][2];
  /// Directory.
  Chunk _directory;
};

Stripe::Chunk::~Chunk()
{
  this->clear();
}
void
Stripe::Chunk::append(MemView m)
{
  _chain.push_back(m);
}
void
Stripe::Chunk::clear()
{
  for (auto &m : _chain)
    free(const_cast<void *>(m.ptr()));
  _chain.clear();
}

Stripe::Stripe(Span *span, Bytes start, CacheStoreBlocks len) : _span(span), _start(start), _len(len)
{
}

bool
Stripe::isFree() const
{
  return 0 == _vol_idx;
}

// Need to be bit more robust at some point.
bool
Stripe::validateMeta(StripeMeta const *meta)
{
  // Need to be bit more robust at some point.
  return StripeMeta::MAGIC == meta->magic && meta->version.ink_major <= ts::CACHE_DB_MAJOR_VERSION &&
         meta->version.ink_minor <= 2 // This may have always been zero, actually.
    ;
}

bool
Stripe::probeMeta(MemView &mem, StripeMeta const* base_meta)
{
  while (mem.size() >= sizeof(StripeMeta)) {
    StripeMeta const* meta = mem.template at_ptr<StripeMeta>(0);
    if (this->validateMeta(meta) &&
        (base_meta == nullptr || // no base version to check against.
         ( meta->version == base_meta->version ) // need more checks here I think.
          ))
    {
      return true;
    }
    // The meta data is stored aligned on a stripe block boundary, so only need to check there.
    mem += CacheStoreBlocks::SCALE;
  }
  return false;
}

void
Stripe::updateLiveData(enum Copy c)
{
  CacheStoreBlocks delta{_meta_pos[c][FOOT] - _meta_pos[c][HEAD]};
  CacheStoreBlocks header_len(0);
  int64_t n_buckets;
  int64_t n_segments;

  // Past the header is the segment free list heads which if sufficiently long (> ~4K) can take
  // more than 1 store block. Start with a guess of 1 and adjust upwards as needed. A 2TB stripe
  // with an AOS of 8000 has roughly 3700 segments meaning that for even 10TB drives this loop
  // should only be a few iterations.
  do {
    ++header_len;
    n_buckets  = (delta - header_len).units() / (sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET);
    n_segments = n_buckets / ts::MAX_BUCKETS_PER_SEGMENT;
    // This should never be more than one loop, usually none.
    while ((n_buckets / n_segments) > ts::MAX_BUCKETS_PER_SEGMENT)
      ++n_segments;
  } while (Bytes(sizeof(StripeMeta) + sizeof(uint16_t) * n_segments) > header_len);

  _buckets  = n_buckets / n_segments;
  _segments = n_segments;
}

Errata
Stripe::loadMeta()
{
  // Read from disk in chunks of this size.
  constexpr static int64_t N = 1 << 24;

  Errata zret;

  int fd = _span->_fd;
  Bytes n;
  bool found;
  MemView data; // The current view of the read buffer.
  Bytes delta;
  Bytes pos = _start;
  // Avoid searching the entire span, because some of it must be content. Assume that AOS is more than 160
  // which means at most 10/160 (1/16) of the span can be directory/header.
  Bytes limit = pos + _len / 16;
  size_t io_align = _span->_geometry.blocksz;
  StripeMeta const *meta;

  std::unique_ptr<char> bulk_buff; // Buffer for bulk reads.
  static const size_t SBSIZE = CacheStoreBlocks::SCALE; // save some typing.
  alignas(SBSIZE) char stripe_buff[SBSIZE]; // Use when reading a single stripe block.

  if (io_align > SBSIZE) return Errata::Message(0,1,"Cannot load stripe ", _idx, " on span ", _span->_path, " because the I/O block alignment ", io_align, " is larger than the buffer alignment ", SBSIZE);

  _directory._start = pos;

  // Header A must be at the start of the stripe block.
  n = pread(fd, stripe_buff, SBSIZE, pos.units());
  data.setView(stripe_buff, n.units());
  meta = data.template at_ptr<StripeMeta>(0);
  if (this->validateMeta(meta)) {
    delta              = data.template at_ptr<char>(0) - bulk_buff.get();
    _meta[A][HEAD]     = *meta;
    _meta_pos[A][HEAD] = round_down(pos + delta);
    pos += SBSIZE;
    // Search for Footer A, skipping false positives.
    while (pos < limit) {
      char *buff = static_cast<char*>(ats_memalign(io_align, N));
      bulk_buff.reset(buff);
      n = pread(fd, buff, N, pos.units());
      data.setView(buff, n.units());
      found = this->probeMeta(data, &_meta[A][HEAD]);
      if (found) {
        ptrdiff_t diff = data.template at_ptr<char>(0) - buff;
        _meta_pos[A][FOOT] = round_down(pos + Bytes(diff));
        if (diff > 0) {
          _directory._clip   = N - diff;
          _directory.append({bulk_buff.release(), N});
        }
        break;
      } else {
        _directory.append({bulk_buff.release(), N});
        pos += N;
      }
    }
  } else {
    zret.push(0, 1, "Header A not found");
  }

  // Technically if Copy A is valid, Copy B is not needed. But at this point it's cheap to retrieve
  // (as the exact offset is computable).
  if (_meta_pos[A][FOOT] > 0) {
    delta = _meta_pos[A][FOOT] - _meta_pos[A][HEAD];
    // Header B should be immediately after Footer A. If at the end of the last read,
    // do another read.
    if (data.size() < CacheStoreBlocks::SCALE) {
      pos += N;
      n = pread(fd, stripe_buff, CacheStoreBlocks::SCALE, pos.units());
      data.setView(stripe_buff, n.units());
    }
    meta = data.template at_ptr<StripeMeta>(0);
    if (this->validateMeta(meta)) {
      _meta[B][HEAD]     = *meta;
      _meta_pos[B][HEAD] = round_down(pos);

      // Footer B must be at the same relative offset to Header B as Footer A -> Header A.
      pos += delta;
      n = pread(fd, stripe_buff, ts::CacheStoreBlocks::SCALE, pos.units());
      data.setView(stripe_buff, n.units());
      meta = data.template at_ptr<StripeMeta>(0);
      if (this->validateMeta(meta)) {
        _meta[B][FOOT]     = *meta;
        _meta_pos[B][FOOT] = round_down(pos);
      }
    }
  }

  if (_meta_pos[A][FOOT] > 0) {
    if (_meta[A][HEAD].sync_serial == _meta[A][FOOT].sync_serial &&
        (0 == _meta_pos[B][FOOT] || _meta[B][HEAD].sync_serial != _meta[B][FOOT].sync_serial ||
         _meta[A][HEAD].sync_serial > _meta[B][HEAD].sync_serial)) {
      this->updateLiveData(A);
    } else if (_meta_pos[B][FOOT] > 0 && _meta[B][HEAD].sync_serial == _meta[B][FOOT].sync_serial) {
      this->updateLiveData(B);
    } else {
      zret.push(0, 1, "Invalid stripe data - candidates found but sync serial data not valid.");
    }
  }

  if (!zret)
    _directory.clear();
  return zret;
}

/* --------------------------------------------------------------------------------------- */
/// A live volume.
/// Volume data based on data from loaded spans.
struct Volume {
  int _idx;               ///< Volume index.
  CacheStoreBlocks _size; ///< Amount of storage allocated.
  std::vector<Stripe *> _stripes;
};
/* --------------------------------------------------------------------------------------- */
/// Data parsed from the volume config file.
struct VolumeConfig {
  Errata load(FilePath const &path);

  /// Data direct from the config file.
  struct Data {
    int _idx        = 0;      ///< Volume index.
    int _percent    = 0;      ///< Size if specified as a percent.
    Megabytes _size = 0;      ///< Size if specified as an absolute.
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

  Errata validatePercentAllocation();
  void convertToAbsolute(ts::CacheStripeBlocks total_span_size);
};

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

void
VolumeConfig::convertToAbsolute(ts::CacheStripeBlocks n)
{
  for (auto &vol : _volumes) {
    if (vol._percent) {
      vol._alloc = (n * vol._percent + 99) / 100;
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
  Errata loadSpanDirect(FilePath const &path, int vol_idx = -1, Bytes size = -1);

  /// Change the @a span to have a single, unused stripe occupying the entire @a span.
  Errata clearSpan(Span *span);

  enum class SpanDumpDepth { SPAN, STRIPE, DIRECTORY };
  void dumpSpans(SpanDumpDepth depth);
  void dumpVolumes();

  ts::CacheStripeBlocks calcTotalSpanPhysicalSize();
  ts::CacheStripeBlocks calcTotalSpanConfiguredSize();

  std::list<Span *> _spans;
  std::map<int, Volume> _volumes;
};
/* --------------------------------------------------------------------------------------- */
/// Temporary structure used for doing allocation computations.
class VolumeAllocator
{
  /// Working struct that tracks allocation information.
  struct V {
    VolumeConfig::Data const &_config; ///< Configuration instance.
    CacheStripeBlocks _size;           ///< Current actual size.
    int64_t _deficit;
    int64_t _shares;

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
};

VolumeAllocator::VolumeAllocator()
{
}

Errata
VolumeAllocator::load(FilePath const &spanFile, FilePath const &volumeFile)
{
  Errata zret;

  if (!volumeFile)
    zret.push(0, 9, "Volume config file not set");
  if (!spanFile)
    zret.push(0, 9, "Span file not set");

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
          if (spot != _cache._volumes.end())
            size = round_down(spot->second._size);
          _av.push_back({vol, size, 0, 0});
        }
      }
    }
  }
  return zret;
}

Errata
VolumeAllocator::fillEmptySpans()
{
  Errata zret;

  /// Scaling factor for shares, effectively the accuracy.
  static const int64_t SCALE = 1000;

  // Walk the spans, skipping ones that are not empty.
  for (auto span : _cache._spans) {
    int64_t total_shares = 0;

    if (!span->isEmpty())
      continue;

    std::cout << "Allocating " << CacheStripeBlocks(round_down(span->_len)) << " from span " << span->_path << std::endl;

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
    // Now allocate blocks.
    ts::CacheStripeBlocks span_blocks = round_up(span->_free_space);
    ts::CacheStripeBlocks span_used(0);

    // sort by deficit so least relatively full volumes go first.
    std::sort(_av.begin(), _av.end(), [](V const &lhs, V const &rhs) { return lhs._deficit > rhs._deficit; });
    for (auto &v : _av) {
      if (v._shares) {
        auto n     = (((span_blocks - span_used) * v._shares) + total_shares - 1) / total_shares;
        auto delta = v._config._alloc - v._size;
        // Not sure why this is needed. But a large and empty volume can dominate the shares
        // enough to get more than it actually needs if the other volume are relative small or full.
        // I need to do more math to see if the weighting can be adjusted to not have this happen.
        n = std::min(n, delta);
        v._size += n;
        span_used += n;
        total_shares -= v._shares;
        span->allocStripe(v._config._idx, n);
        std::cout << "           " << n << " to volume " << v._config._idx << std::endl;
      }
    }
    std::cout << "     Total " << span_used << std::endl;
    std::cout << " Updating Header ... ";
    zret = span->updateHeader();
    if (zret)
      std::cout << " Done" << std::endl;
    else
      std::cout << " Error" << std::endl << zret;
  }
  return zret;
}
/* --------------------------------------------------------------------------------------- */
Errata
Cache::loadSpan(FilePath const &path)
{
  Errata zret;
  if (!path.is_readable())
    zret = Errata::Message(0, EPERM, path, " is not readable.");
  else if (path.is_regular_file())
    zret = this->loadSpanConfig(path);
  else
    zret = this->loadSpanDirect(path);
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
        stripe->_idx = i;
        if (raw.free == 0) {
          stripe->_vol_idx = raw.vol_idx;
          stripe->_type    = raw.type;
          _volumes[stripe->_vol_idx]._stripes.push_back(stripe);
          _volumes[stripe->_vol_idx]._size += stripe->_len;
        } else {
          span->_free_space += stripe->_len;
        }
        span->_stripes.push_back(stripe);
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
  static const ts::StringView TAG_ID("id");
  static const ts::StringView TAG_VOL("volume");

  Errata zret;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::StringView content = cfile.content();
    while (content) {
      ts::StringView line = content.splitPrefix('\n');
      line.ltrim(&isspace);
      if (!line || '#' == *line)
        continue;
      ts::StringView path = line.extractPrefix(&isspace);
      if (path) {
        // After this the line is [size] [id=string] [volume=#]
        while (line) {
          ts::StringView value(line.extractPrefix(&isspace));
          if (value) {
            ts::StringView tag(value.splitPrefix('='));
            if (!tag) { // must be the size
            } else if (0 == strcasecmp(tag, TAG_ID)) {
            } else if (0 == strcasecmp(tag, TAG_VOL)) {
              ts::StringView text;
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

void
Cache::dumpSpans(SpanDumpDepth depth)
{
  if (depth >= SpanDumpDepth::SPAN) {
    for (auto span : _spans) {
      if (nullptr == span->_header) {
        std::cout << "Span: " << span->_path << " is uninitialized" << std::endl;
      } else {
        std::cout << "Span: " << span->_path << " " << span->_header->num_volumes << " Volumes " << span->_header->num_used
                  << " in use " << span->_header->num_free << " free " << span->_header->num_diskvol_blks << " stripes "
                  << span->_header->num_blocks.units() << " blocks" << std::endl;

        for (auto stripe : span->_stripes) {
          std::cout << "    : "
                    << " @ " << stripe->_start << " len=" << stripe->_len.count() << " blocks "
                    << " vol=" << static_cast<int>(stripe->_vol_idx) << " type=" << static_cast<int>(stripe->_type) << " "
                    << (stripe->isFree() ? "free" : "in-use") << std::endl;
          if (depth >= SpanDumpDepth::STRIPE) {
            Errata r = stripe->loadMeta();
            if (r) {
              std::cout << "Stripe found: " << stripe->_segments << " segments with " << stripe->_buckets
                        << " buckets per segment for " << stripe->_buckets * stripe->_segments * 4
                        << " total directory entries taking "
                        << stripe->_buckets * stripe->_segments * sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET
                        //                        << " out of " << (delta-header_len).units() << " bytes."
                        << std::endl;
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
    for (auto const &r : elt.second._stripes)
      size += r->_len.units();

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

Cache::~Cache()
{
  for (auto *span : _spans)
    delete span;
}
/* --------------------------------------------------------------------------------------- */
Errata
Span::load()
{
  Errata zret;
  if (!_path.is_readable())
    zret = Errata::Message(0, EPERM, _path, " is not readable.");
  else if (_path.is_char_device() || _path.is_block_device())
    zret = this->loadDevice();
  else if (_path.is_dir())
    zret.push(0, 1, "Directory support not yet available");
  else
    zret.push(0, EBADF, _path, " is not a valid file type");
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

  ats_scoped_fd fd(_path.open(flags));

  if (fd) {
    if (ink_file_get_geometry(fd, _geometry)) {
      off_t offset = ts::CacheSpan::OFFSET.units();
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
          _header.reset(new (malloc(span_hdr_size.units())) ts::SpanHeader);
          if (span_hdr_size.units() <= BUFF_SIZE) {
            memcpy(_header.get(), buff, span_hdr_size.units());
          } else {
            // TODO - check the pread return
            pread(fd, _header.get(), span_hdr_size.units(), offset);
          }
          _len = _header->num_blocks;
        } else {
          zret = Errata::Message(0, 0, "Span header for ", _path, " is invalid");
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
  int n   = (eff.units() - sizeof(ts::SpanHeader)) / (CacheStripeBlocks::SCALE + sizeof(CacheStripeDescriptor));
  _offset = _base + round_up(sizeof(ts::SpanHeader) + (n - 1) * sizeof(CacheStripeDescriptor));
  stripe  = new Stripe(this, _offset, _len - _offset);
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
  void *raw                 = ats_memalign(512, hdr_size.units());
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
    ssize_t r = pwrite(_fd, hdr, hdr_size.units(), ts::CacheSpan::OFFSET.units());
    if (r < ts::CacheSpan::OFFSET.units())
      zret.push(0, errno, "Failed to update span - ", strerror(errno));
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
    ssize_t n = pwrite(_fd, zero, sizeof(zero), ts::CacheSpan::OFFSET.units());
    if (n == sizeof(zero))
      std::cout << "done";
    else {
      const char *text = strerror(errno);
      std::cout << "failed";
      if (n >= 0)
        std::cout << " - " << n << " of " << sizeof(zero) << " bytes written";
      std::cout << " - " << text;
    }
    std::cout << std::endl;
  } else {
    std::cout << "Clearing " << _path << " not performed, write not enabled" << std::endl;
  }
}
/* --------------------------------------------------------------------------------------- */
Errata
VolumeConfig::load(FilePath const &path)
{
  static const ts::StringView TAG_SIZE("size");
  static const ts::StringView TAG_VOL("volume");

  Errata zret;

  int ln = 0;

  ts::BulkFile cfile(path);
  if (0 == cfile.load()) {
    ts::StringView content = cfile.content();
    while (content) {
      Data v;

      ++ln;
      ts::StringView line = content.splitPrefix('\n');
      line.ltrim(&isspace);
      if (!line || '#' == *line)
        continue;

      while (line) {
        ts::StringView value(line.extractPrefix(&isspace));
        ts::StringView tag(value.splitPrefix('='));
        if (!tag) {
          zret.push(0, 1, "Line ", ln, " is invalid");
        } else if (0 == strcasecmp(tag, TAG_SIZE)) {
          if (v.hasSize()) {
            zret.push(0, 5, "Line ", ln, " has field ", TAG_SIZE, " more than once");
          } else {
            ts::StringView text;
            auto n = ts::svtoi(value, &text);
            if (text) {
              ts::StringView percent(text.end(), value.end()); // clip parsed number.
              if (!percent) {
                v._size = round_up(v._size = n);
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
            ts::StringView text;
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
        if (!v.hasSize())
          zret.push(0, 7, "Line ", ln, " does not have the required field ", TAG_SIZE);
        if (!v.hasIndex())
          zret.push(0, 8, "Line ", ln, " does not have the required field ", TAG_VOL);
      }
    }
  } else {
    zret = Errata::Message(0, EBADF, "Unable to load ", path);
  }
  return zret;
}
/* --------------------------------------------------------------------------------------- */
struct option Options[] = {{"help", 0, nullptr, 'h'},
                           {"spans", 1, nullptr, 's'},
                           {"volumes", 1, nullptr, 'v'},
                           {"write", 0, nullptr, 'w'},
                           {nullptr, 0, nullptr, 0}};
}

Errata
List_Stripes(Cache::SpanDumpDepth depth, int argc, char *argv[])
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
  VolumeConfig vols;
  Cache cache;

  if (!VolumeFile)
    zret.push(0, 9, "Volume config file not set");
  if (!SpanFile)
    zret.push(0, 9, "Span file not set");

  if (zret) {
    zret = vols.load(VolumeFile);
    if (zret) {
      zret = cache.loadSpan(SpanFile);
      if (zret) {
        ts::CacheStripeBlocks total = cache.calcTotalSpanConfiguredSize();
        struct V {
          int idx;
          ts::CacheStripeBlocks alloc; // target allocation
          ts::CacheStripeBlocks size;  // actually allocated space
          int64_t deficit;
          int64_t shares;
        };
        std::vector<V> av;
        vols.convertToAbsolute(total);
        for (auto &vol : vols) {
          ts::CacheStripeBlocks size(0);
          auto spot = cache._volumes.find(vol._idx);
          if (spot != cache._volumes.end())
            size = round_down(spot->second._size);
          av.push_back({vol._idx, vol._alloc, size, 0, 0});
        }
        for (auto span : cache._spans) {
          if (span->_free_space <= 0)
            continue;
          static const int64_t SCALE = 1000;
          int64_t total_shares       = 0;
          for (auto &v : av) {
            auto delta = v.alloc - v.size;
            if (delta > 0) {
              v.deficit = (delta.count() * SCALE) / v.alloc.count();
              v.shares  = delta.count() * v.deficit;
              total_shares += v.shares;
              std::cout << "Volume " << v.idx << " allocated " << v.alloc << " has " << v.size << " needs " << (v.alloc - v.size)
                        << " deficit " << v.deficit << std::endl;
            } else {
              v.shares = 0;
            }
          }
          // Now allocate blocks.
          ts::CacheStripeBlocks span_blocks = round_down(span->_free_space);
          ts::CacheStripeBlocks span_used(0);
          std::cout << "Allocation from span of " << span_blocks << std::endl;
          // sort by deficit so least relatively full volumes go first.
          std::sort(av.begin(), av.end(), [](V const &lhs, V const &rhs) { return lhs.deficit > rhs.deficit; });
          for (auto &v : av) {
            if (v.shares) {
              auto n     = (((span_blocks - span_used) * v.shares) + total_shares - 1) / total_shares;
              auto delta = v.alloc - v.size;
              // Not sure why this is needed. But a large and empty volume can dominate the shares
              // enough to get more than it actually needs if the other volume are relative small or full.
              // I need to do more math to see if the weighting can be adjusted to not have this happen.
              n = std::min(n, delta);
              v.size += n;
              span_used += n;
              std::cout << "Volume " << v.idx << " allocated " << n << " of " << delta << " needed to total of " << v.size << " of "
                        << v.alloc << std::endl;
              std::cout << "         with " << v.shares << " shares of " << total_shares << " total - "
                        << static_cast<double>((v.shares * SCALE) / total_shares) / 10.0 << "%" << std::endl;
              total_shares -= v.shares;
            }
          }
          std::cout << "Span allocated " << span_used << " of " << span_blocks << std::endl;
        }
      }
    }
  }
  return zret;
}

Errata
Clear_Spans(int argc, char *argv[])
{
  Errata zret;

  Cache cache;
  //  OPEN_RW_FLAG = O_RDWR;
  if ((zret = cache.loadSpan(SpanFile))) {
    for (auto *span : cache._spans) {
      span->clearPermanently();
    }
  }

  return zret;
}

int
main(int argc, char *argv[])
{
  int opt_idx = 0;
  int opt_val;
  bool help = false;
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
    }
  }

  Commands
    .add(std::string("list"), std::string("List elements of the cache"),
         [](int argc, char *argv[]) { return List_Stripes(Cache::SpanDumpDepth::SPAN, argc, argv); })
    .subCommand(std::string("stripes"), std::string("The stripes"),
                [](int argc, char *argv[]) { return List_Stripes(Cache::SpanDumpDepth::STRIPE, argc, argv); });
  Commands.add(std::string("clear"), std::string("Clear spans"), &Clear_Spans);
  Commands.add(std::string("volumes"), std::string("Volumes"), &Simulate_Span_Allocation);
  Commands.add(std::string("alloc"), std::string("Storage allocation"))
    .subCommand(std::string("free"), std::string("Allocate storage on free (empty) spans"), &Cmd_Allocate_Empty_Spans);

  Commands.setArgIndex(optind);

  if (help) {
    Commands.helpMessage(argc, argv);
    exit(1);
  }

  Errata result = Commands.invoke(argc, argv);

  if (result.size()) {
    std::cerr << result;
  }
  return 0;
}
