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
#include <ts/MemView.h>
#include <getopt.h>
#include <system_error>
#include <fcntl.h>
#include <ctype.h>
#include "File.h"
#include "CacheDefs.h"
#include "Command.h"

// Sigh, a hack for now. We already have "ts" defined as a namespace in various places so for now
// just import the Full Name namespace in to 'ts' rather than direct 'namespace ts = ApachTrafficServer'
namespace ts
{
using namespace ApacheTrafficServer;
}

namespace ApacheTrafficServer
{
const Bytes CacheSpan::OFFSET{CacheStoreBlocks{1}};
}

namespace
{
ts::FilePath TargetFile;
ts::FilePath VolumeFile;
ts::CommandTable Commands;
// Default this to read only, only enable write if specifically required.
int OPEN_RW_FLAGS = O_RDONLY;

struct Span {
  Span(ts::FilePath const &path) : _path(path) {}
  void clearPermanently();

  ts::FilePath _path;
  ats_scoped_fd _fd;
  std::unique_ptr<ts::SpanHeader> _header;
};

struct Volume {
  struct StripeRef {
    Span *_span; ///< Span with stripe.
    int _idx;    ///< Stripe index in span.
  };
  int _idx; ///< Volume index.
  std::vector<StripeRef> _stripes;
};

// Data parsed from the volume config file.
struct VolumeConfig
{
  ts::Errata load(ts::FilePath const& path);

  struct VolData
  {
    int _idx; ///< Volume index.
    int _percent; ///< Size if specified as a percent.
    ts::CacheStripeBlocks _size; ///< Size if specified as an absolute.
  };

  std::vector<VolData> _volumes;
};

// All of these free functions need to be moved to the Cache class.

bool
Validate_Stripe_Meta(ts::CacheStripeMeta const &stripe)
{
  return ts::CacheStripeMeta::MAGIC == stripe.magic && stripe.version.ink_major <= ts::CACHE_DB_MAJOR_VERSION &&
         stripe.version.ink_minor <= 2 // This may have always been zero, actually.
    ;
}

typedef std::tuple<int, ts::StringView> ProbeResult;

ProbeResult
Probe_For_Stripe(ts::StringView &mem)
{
  ProbeResult zret{mem.size() >= sizeof(ts::CacheStripeMeta) ? 0 : -1, ts::StringView(nullptr)};
  ts::StringView &test_site = std::get<1>(zret);

  while (mem.size() >= sizeof(ts::CacheStripeMeta)) {
    // The meta data is stored aligned on a stripe block boundary, so only need to check there.
    test_site = mem;
    mem += ts::CacheStoreBlocks::SCALE; // always move this forward to make restarting search easy.

    if (Validate_Stripe_Meta(*reinterpret_cast<ts::CacheStripeMeta const *>(test_site.ptr()))) {
      std::get<0>(zret) = 1;
      break;
    }
  }
  return zret;
}

void
Calc_Stripe_Data(ts::CacheStripeMeta const &header, ts::CacheStripeMeta const &footer, off_t delta, ts::StripeData &data)
{
  // Assuming header + free list fits in one cache stripe block, which isn't true for large stripes (>2G or so).
  // Need to detect that, presumably by checking that the segment count fits in the stripe block.
  ts::CacheStoreBlocks hdr_size{1};
  off_t space       = delta - hdr_size.units();
  int64_t n_buckets = space / 40;
  data.segments     = n_buckets / (1 << 14);
  // This should never be more than one loop, usually none.
  while ((n_buckets / data.segments) > 1 << 14)
    ++(data.segments);
  data.buckets = n_buckets / data.segments;
  data.start   = delta * 2; // this is wrong, need to add in the base block position.

  std::cout << "Stripe is " << data.segments << " segments with " << data.buckets << " buckets per segment for "
            << data.buckets * data.segments * 4 << " total directory entries taking " << data.buckets * data.segments * 40
            << " out of " << space << " bytes." << std::endl;
}

void
Open_Stripe(ats_scoped_fd const &fd, ts::CacheStripeDescriptor const &block)
{
  int found;
  ts::StringView data;
  ts::StringView stripe_mem;
  constexpr static int64_t N = 1 << 24;
  int64_t n;
  off_t pos = block.offset.units();
  ts::CacheStripeMeta stripe_meta[4];
  off_t stripe_pos[4] = {0, 0, 0, 0};
  off_t delta;
  // Avoid searching the entire span, because some of it must be content. Assume that AOS is more than 160
  // which means at most 10/160 (1/16) of the span can be directory/header.
  off_t limit = pos + block.len.units() / 16;
  alignas(4096) static char buff[N];

  // Check the earlier part of the block. Header A must be at the start of the stripe block.
  // A full chunk is read in case Footer A is in that range.
  n = pread(fd, buff, N, pos);
  data.setView(buff, n);
  std::tie(found, stripe_mem) = Probe_For_Stripe(data);

  if (found > 0) {
    if (stripe_mem.ptr() != buff) {
      std::cout << "Header A found at" << pos + stripe_mem.ptr() - buff << " which is not at start of stripe block" << std::endl;
    } else {
      stripe_pos[0]  = pos;
      stripe_meta[0] = reinterpret_cast<ts::CacheStripeMeta &>(buff); // copy it out of buffer.
      std::cout << "Header A found at " << stripe_pos[0] << std::endl;
      // Search for Footer A, skipping false positives.
      while (stripe_pos[1] == 0) {
        std::tie(found, stripe_mem) = Probe_For_Stripe(data);
        while (found == 0 && pos < limit) {
          pos += N;
          n = pread(fd, buff, N, pos);
          data.setView(buff, n);
          std::tie(found, stripe_mem) = Probe_For_Stripe(data);
        }
        if (found > 0) {
          // Need to be more thorough in cross checks but this is OK for now.
          ts::CacheStripeMeta const &s = *reinterpret_cast<ts::CacheStripeMeta const *>(stripe_mem.ptr());
          if (s.version == stripe_meta[0].version) {
            stripe_meta[1] = s;
            stripe_pos[1]  = pos + (stripe_mem.ptr() - buff);
            printf("Footer A found at %" PRIu64 "\n", stripe_pos[1]);
            if (stripe_meta[0].sync_serial == stripe_meta[1].sync_serial) {
              printf("Copy A is valid - sync=%d\n", stripe_meta[0].sync_serial);
            }
          } else {
            // false positive, keep looking.
            found = 0;
          }
        } else {
          printf("Header A not found, invalid stripe.\n");
          break;
        }
      }

      // Technically if Copy A is valid, Copy B is not needed. But at this point it's cheap to retrieve
      // (as the exact offsets are computable).
      if (stripe_pos[1]) {
        delta = stripe_pos[1] - stripe_pos[0];
        // Header B should be immediately after Footer A. If at the end of the last read,
        // do another read.
        if (!data) {
          pos += N;
          n = pread(fd, buff, ts::CacheStoreBlocks::SCALE, pos);
          data.setView(buff, n);
        }
        std::tie(found, stripe_mem) = Probe_For_Stripe(data);
        if (found <= 0) {
          printf("Header B not found at expected location.\n");
        } else {
          stripe_meta[2] = *reinterpret_cast<ts::CacheStripeMeta const *>(stripe_mem.ptr());
          stripe_pos[2]  = pos + (stripe_mem.ptr() - buff);
          printf("Found Header B at expected location %" PRIu64 ".\n", stripe_pos[2]);

          // Footer B must be at the same relative offset to Header B as Footer A -> Header A.
          n = pread(fd, buff, ts::CacheStoreBlocks::SCALE, stripe_pos[2] + delta);
          data.setView(buff, n);
          std::tie(found, stripe_mem) = Probe_For_Stripe(data);
          if (found == 1) {
            stripe_pos[3]  = stripe_pos[2] + delta;
            stripe_meta[3] = *reinterpret_cast<ts::CacheStripeMeta const *>(stripe_mem.ptr());
            printf("Footer B found at expected location %" PRIu64 ".\n", stripe_pos[3]);
          } else {
            printf("Footer B not found at expected location %" PRIu64 ".\n", stripe_pos[2] + delta);
          }
        }
      }

      if (stripe_pos[1]) {
        if (stripe_meta[0].sync_serial == stripe_meta[1].sync_serial &&
            (0 == stripe_pos[3] || stripe_meta[2].sync_serial != stripe_meta[3].sync_serial ||
             stripe_meta[0].sync_serial > stripe_meta[2].sync_serial)) {
          ts::StripeData sdata;
          Calc_Stripe_Data(stripe_meta[0], stripe_meta[1], delta, sdata);
        } else if (stripe_pos[3] && stripe_meta[2].sync_serial == stripe_meta[3].sync_serial) {
          ts::StripeData sdata;
          Calc_Stripe_Data(stripe_meta[2], stripe_meta[3], delta, sdata);
        } else {
          std::cout << "Invalid stripe data - candidates found but sync serial data not valid." << std::endl;
        }
      } else {
        std::cout << "Invalid stripe data - no candidates found." << std::endl;
      }
    }
  } else {
    printf("Stripe Header A not found in first chunk\n");
  }
}

// --------------------
struct Cache {
  ~Cache();

  ts::Errata load(ts::FilePath const &path);
  void loadConfig(ts::FilePath const &path);
  void loadDevice(ts::FilePath const &path);

  enum class SpanDumpDepth { SPAN, STRIPE, DIRECTORY };
  void dumpSpans(SpanDumpDepth depth);
  void dumpVolumes();

  std::list<Span *> _spans;
  std::map<int, Volume> _volumes;
};

ts::Errata
Cache::load(ts::FilePath const &path)
{
  ts::Errata zret;
  if (!path.is_readable())
    zret = ts::Errata::Message(0,0,path," is not readable");
//    throw(std::system_error(errno, std::system_category(), static_cast<char const *>(path)));
  else if (path.is_regular_file())
    this->loadConfig(path);
  else if (path.is_char_device() || path.is_block_device())
    this->loadDevice(path);
  else
    printf("Not a valid file type: '%s'\n", static_cast<char const *>(path));
  return zret;
}

void
Cache::loadConfig(ts::FilePath const &path)
{
  static const ts::StringView TAG_ID("id");
  static const ts::StringView TAG_VOL("volume");

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
        // After this the line is [size] [id=string] [vol=#]
        while (line) {
          ts::StringView value(line.extractPrefix(&isspace));
          if (value) {
            ts::StringView tag(value.splitPrefix('='));
            if (!tag) {
            } else if (0 == strcasecmp(tag, TAG_ID)) {
            } else if (0 == strcasecmp(tag, TAG_VOL)) {
            }
          }
        }
        this->load(ts::FilePath(path));
      }
    }
  }
}

void
Cache::loadDevice(ts::FilePath const &path)
{
  int flags;

  flags = OPEN_RW_FLAGS
#if defined(O_DIRECT)
          | O_DIRECT
#endif
#if defined(O_DSYNC)
          | O_DSYNC
#endif
    ;

  ats_scoped_fd fd(path.open(flags));

  if (fd) {
    off_t offset = ts::CacheSpan::OFFSET.units();
    alignas(512) char buff[8192];
    int64_t n = pread(fd, buff, sizeof(buff), offset);
    if (n >= static_cast<int64_t>(sizeof(ts::SpanHeader))) {
      ts::SpanHeader &span_hdr = reinterpret_cast<ts::SpanHeader &>(buff);
      // See if it looks valid
      if (span_hdr.magic == ts::SpanHeader::MAGIC && span_hdr.num_diskvol_blks == span_hdr.num_used + span_hdr.num_free) {
        int nspb             = span_hdr.num_diskvol_blks;
        size_t span_hdr_size = sizeof(ts::SpanHeader) + (nspb - 1) * sizeof(ts::CacheStripeDescriptor);
        Span *span           = new Span(path);
        span->_header.reset(new (malloc(span_hdr_size)) ts::SpanHeader);
        if (span_hdr_size <= sizeof(buff)) {
          memcpy(span->_header.get(), buff, span_hdr_size);
        } else {
          // TODO - check the pread return
          pread(fd, span->_header.get(), span_hdr_size, offset);
        }
        span->_fd = fd.release();
        _spans.push_back(span);
        for (auto i = 0; i < nspb; ++i) {
          ts::CacheStripeDescriptor &stripe = span->_header->stripes[i];
          if (stripe.free == 0) {
            // Add to volume.
            _volumes[stripe.vol_idx]._stripes.push_back(Volume::StripeRef{span, i});
          }
        }
      }
    } else {
      printf("Failed to read from '%s' [%d]\n", path.path(), errno);
    }
  } else {
    printf("Unable to open '%s'\n", static_cast<char const *>(path));
  }
}

void
Cache::dumpSpans(SpanDumpDepth depth)
{
  if (depth >= SpanDumpDepth::SPAN) {
    for (auto span : _spans) {
      std::cout << "Span: " << span->_path << " " << span->_header->num_volumes << " Volumes " << span->_header->num_used
                << " in use " << span->_header->num_free << " free " << span->_header->num_diskvol_blks << " stripes "
                << span->_header->num_blocks << " blocks" << std::endl;
      for (unsigned int i = 0; i < span->_header->num_diskvol_blks; ++i) {
        ts::CacheStripeDescriptor &stripe = span->_header->stripes[i];
        std::cout << "    : SpanBlock " << i << " @ " << stripe.offset.units() << " blocks=" << stripe.len.units()
                  << " vol=" << stripe.vol_idx << " type=" << stripe.type << " " << (stripe.free ? "free" : "in-use") << std::endl;
        if (depth >= SpanDumpDepth::STRIPE) {
          Open_Stripe(span->_fd, stripe);
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
      size += r._span->_header->stripes[r._idx].len.units();

    std::cout << "Volume " << elt.first << " has " << elt.second._stripes.size() << " stripes and " << size << " bytes"
              << std::endl;
  }
}

Cache::~Cache()
{
  for (auto *span : _spans)
    delete span;
}

void
Span::clearPermanently()
{
  alignas(512) static char zero[ts::CacheStoreBlocks::SCALE]; // should be all zero, it's static.
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
}

struct option Options[] = {{"help", false, nullptr, 'h'}};
}

ts::Errata
List_Stripes(Cache::SpanDumpDepth depth, int argc, char *argv[])
{
  ts::Errata zret;
  Cache cache;

  if ((zret = cache.load(TargetFile))) {
      cache.dumpSpans(depth);
      cache.dumpVolumes();
  }
  return zret;
}

ts::Errata
Simulate_Span_Allocation(int argc, char *argv[])
{
  ts::Errata zret;
  return zret;
}

ts::Errata
Clear_Spans(int argc, char *argv[])
{
  ts::Errata zret;

  Cache cache;
  OPEN_RW_FLAGS = O_RDWR;
  if ((zret = cache.load(TargetFile))) {
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
      printf("Usage: %s [device_path|config_file] <COMMAND> [<SUBCOMMAND> ...]\n", argv[0]);
      help = true;
      break;
    }
  }

  Commands
    .add(std::string("list"), std::string("List elements of the cache"),
         [](int argc, char *argv[]) { return List_Stripes(Cache::SpanDumpDepth::SPAN, argc, argv); })
    .subCommand(std::string("stripes"), std::string("The stripes"),
                [](int argc, char *argv[]) { return List_Stripes(Cache::SpanDumpDepth::STRIPE, argc, argv); });
  Commands.add(std::string("clear"), std::string("Clear spans"), &Clear_Spans);

  if (help) {
    Commands.helpMessage(argc - optind, argv + optind);
    exit(1);
  }

  if (optind < argc) {
    TargetFile = argv[optind];
    argc -= optind + 1;
    argv += optind + 1;
  }
  ts::Errata result = Commands.invoke(argc, argv);

  if (!result) {
    std::cerr << result;
  }
  return 0;
}
