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

#include "CacheDefs.h"
#include <iostream>
#include <fcntl.h>

using namespace std;
using namespace ts;

using ts::Errata;
namespace ts
{
std::ostream &
operator<<(std::ostream &s, Bytes const &n)
{
  return s << n.count() << " bytes";
}
std::ostream &
operator<<(std::ostream &s, Kilobytes const &n)
{
  return s << n.count() << " KB";
}
std::ostream &
operator<<(std::ostream &s, Megabytes const &n)
{
  return s << n.count() << " MB";
}
std::ostream &
operator<<(std::ostream &s, Gigabytes const &n)
{
  return s << n.count() << " GB";
}
std::ostream &
operator<<(std::ostream &s, Terabytes const &n)
{
  return s << n.count() << " TB";
}

std::ostream &
operator<<(std::ostream &s, CacheStripeBlocks const &n)
{
  return s << n.count() << " stripe blocks";
}
std::ostream &
operator<<(std::ostream &s, CacheStoreBlocks const &n)
{
  return s << n.count() << " store blocks";
}
std::ostream &
operator<<(std::ostream &s, CacheDataBlocks const &n)
{
  return s << n.count() << " data blocks";
}

Errata
URLparser::parseURL(TextView URI)
{
  Errata zret;
  static const TextView HTTP("http");
  static const TextView HTTPS("https");
  TextView scheme = URI.take_prefix_at(':');
  if ((strcasecmp(scheme, HTTP) == 0) || (strcasecmp(scheme, HTTPS) == 0)) {
    TextView hostname = URI.take_prefix_at(':');
    if (!hostname) // i.e. port not present
    {
    }
  }

  return zret;
}

int
URLparser::getPort(std::string &fullURL, int &port_ptr, int &port_len)
{
  url_matcher matcher;
  int n_port = -1;
  int u_pos  = -1;

  if (fullURL.find("https") == 0) {
    u_pos  = 8;
    n_port = 443;
  } else if (fullURL.find("http") == 0) {
    u_pos  = 7;
    n_port = 80;
  }
  if (u_pos != -1) {
    fullURL.insert(u_pos, ":@");
    TextView url(fullURL.data(), (int)fullURL.size());

    url += 9;

    TextView hostPort = url.take_prefix_at(':');
    if (!hostPort.empty()) // i.e. port is present
    {
      TextView port = url.take_prefix_at('/');
      if (port.empty()) { // i.e. backslash is not present, then the rest of the url must be just port
        port = url;
      }
      if (matcher.portmatch(port.data(), port.size())) {
        TextView text;
        n_port = svtoi(port, &text);
        if (text == port) {
          port_ptr = fullURL.find(':', 9);
          port_len = port.size();
          return n_port;
        }
      }
    }
    return n_port;
  } else {
    std::cout << "No scheme provided for: " << fullURL << std::endl;
    return -1;
  }
}

uint32_t
Doc::prefix_len()
{
  return sizeof(Doc) + hlen;
}

uint32_t
Doc::data_len()
{
  return len - sizeof(Doc) - hlen;
}

int
Doc::single_fragment()
{
  return data_len() == total_len;
}

char *
Doc::hdr()
{
  return reinterpret_cast<char *>(this) + sizeof(Doc);
}

char *
Doc::data()
{
  return this->hdr() + hlen;
}
} // end namespace ts

int cache_config_min_average_object_size = ESTIMATED_OBJECT_SIZE;
CacheStoreBlocks Vol_hash_alloc_size(1024);
// Default this to read only, only enable write if specifically required.
int OPEN_RW_FLAG = O_RDONLY;

namespace ct
{
bool
Stripe::validate_sync_serial()
{
  // check if A sync_serials match and A is at least as updated as B
  return (_meta[0][0].sync_serial == _meta[0][1].sync_serial &&
          (_meta[0][0].sync_serial >= _meta[1][0].sync_serial ||
           _meta[1][0].sync_serial != _meta[1][1].sync_serial)) || // OR check if B's sync_serials match
         (_meta[1][0].sync_serial == _meta[1][1].sync_serial);
}

Errata
Stripe::clear()
{
  Errata zret;
  alignas(512) static char zero[CacheStoreBlocks::SCALE]; // should be all zero, it's static.
  for (auto i : {A, B}) {
    for (auto j : {HEAD, FOOT}) {
      ssize_t n = pwrite(_span->_fd, zero, CacheStoreBlocks::SCALE, this->_meta_pos[i][j]);
      if (n < CacheStoreBlocks::SCALE) {
        std::cout << "Failed to clear stripe header" << std::endl;
      }
    }
  }

  return zret;
}
Stripe::Chunk::~Chunk()
{
  this->clear();
}
void
Stripe::Chunk::append(MemSpan m)
{
  _chain.push_back(m);
}
void
Stripe::Chunk::clear()
{
  for (auto &m : _chain) {
    free(const_cast<void *>(m.data()));
  }
  _chain.clear();
}

Stripe::Stripe(Span *span, Bytes start, CacheStoreBlocks len) : _span(span), _start(start), _len(len)
{
  ts::bwprint(hashText, "{} {}:{}", span->_path.view(), _start.count(), _len.count());
  CryptoContext().hash_immediate(hash_id, hashText.data(), static_cast<int>(hashText.size()));
  printf("hash id of stripe is hash of %.*s\n", static_cast<int>(hashText.size()), hashText.data());
}

bool
Stripe::isFree() const
{
  return 0 == _vol_idx;
}

// TODO: Implement the whole logic
Errata
Stripe::InitializeMeta()
{
  Errata zret;
  // memset(this->raw_dir, 0, dir_len);
  for (auto &i : _meta) {
    for (auto &j : i) {
      j.magic          = StripeMeta::MAGIC;
      j.version._major = ts::CACHE_DB_MAJOR_VERSION;
      j.version._minor = ts::CACHE_DB_MINOR_VERSION;
      j.agg_pos = j.last_write_pos = j.write_pos = this->_content;
      j.phase = j.cycle = j.sync_serial = j.write_serial = j.dirty = 0;
      j.create_time                                                = time(nullptr);
      j.sector_size                                                = DEFAULT_HW_SECTOR_SIZE;
    }
  }
  if (!freelist) // freelist is not allocated yet
  {
    freelist = (uint16_t *)malloc(_segments * sizeof(uint16_t)); // segments has already been calculated
  }
  if (!dir) // for new spans, this will likely be nullptr as we don't need to read the stripe meta from disk
  {
    char *raw_dir = (char *)ats_memalign(ats_pagesize(), this->vol_dirlen());
    dir           = (CacheDirEntry *)(raw_dir + this->vol_headerlen());
  }
  init_dir();
  return zret;
}

// Need to be bit more robust at some point.
bool
Stripe::validateMeta(StripeMeta const *meta)
{
  // Need to be bit more robust at some point.
  return StripeMeta::MAGIC == meta->magic && meta->version._major <= ts::CACHE_DB_MAJOR_VERSION &&
         meta->version._minor <= 2 // This may have always been zero, actually.
    ;
}

bool
Stripe::probeMeta(MemSpan &mem, StripeMeta const *base_meta)
{
  while (mem.size() >= sizeof(StripeMeta)) {
    StripeMeta const *meta = mem.ptr<StripeMeta>(0);
    if (this->validateMeta(meta) && (base_meta == nullptr ||               // no base version to check against.
                                     (meta->version == base_meta->version) // need more checks here I think.
                                     )) {
      return true;
    }
    // The meta data is stored aligned on a stripe block boundary, so only need to check there.
    mem += CacheStoreBlocks::SCALE;
  }
  return false;
}

Errata
Stripe::updateHeaderFooter()
{
  Errata zret;
  this->vol_init_data();

  int64_t hdr_size    = this->vol_headerlen();
  int64_t dir_size    = this->vol_dirlen();
  Bytes footer_offset = Bytes(dir_size - ROUND_TO_STORE_BLOCK(sizeof(StripeMeta)));
  _meta_pos[A][HEAD]  = round_down(_start);
  _meta_pos[A][FOOT]  = round_down(_start + footer_offset);
  _meta_pos[B][HEAD]  = round_down(this->_start + Bytes(dir_size));
  _meta_pos[B][FOOT]  = round_down(this->_start + Bytes(dir_size) + footer_offset);
  std::cout << "updating header " << _meta_pos[0][0] << std::endl;
  std::cout << "updating header " << _meta_pos[0][1] << std::endl;
  std::cout << "updating header " << _meta_pos[1][0] << std::endl;
  std::cout << "updating header " << _meta_pos[1][1] << std::endl;
  InitializeMeta();

  if (!OPEN_RW_FLAG) {
    zret.push(0, 1, "Writing Not Enabled.. Please use --write to enable writing to disk");
    return zret;
  }

  char *meta_t = (char *)ats_memalign(ats_pagesize(), dir_size);
  // copy headers
  for (auto i : {A, B}) {
    // copy header
    memcpy(meta_t, &_meta[i][HEAD], sizeof(StripeMeta));
    // copy freelist
    memcpy(meta_t + sizeof(StripeMeta) - sizeof(uint16_t), this->freelist, this->_segments * sizeof(uint16_t));

    ssize_t n = pwrite(_span->_fd, meta_t, hdr_size, _meta_pos[i][HEAD]);
    if (n < hdr_size) {
      std::cout << "problem writing header to disk: " << strerror(errno) << ":"
                << " " << n << "<" << hdr_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      ats_free(meta_t);
      return zret;
    }
    // copy dir entries
    dir_size = dir_size - hdr_size - ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
    memcpy(meta_t, (char *)dir, dir_size);
    n = pwrite(_span->_fd, meta_t, dir_size, _meta_pos[i][HEAD] + hdr_size); //
    if (n < dir_size) {
      std::cout << "problem writing dir to disk: " << strerror(errno) << ":"
                << " " << n << "<" << dir_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      ats_free(meta_t);
      return zret;
    }

    // copy footer,
    memcpy(meta_t, &_meta[i][FOOT], sizeof(StripeMeta));

    int64_t footer_size = ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
    n                   = pwrite(_span->_fd, meta_t, footer_size, _meta_pos[i][FOOT]);
    if (n < footer_size) {
      std::cout << "problem writing footer to disk: " << strerror(errno) << ":"
                << " " << n << "<" << footer_size << std::endl;
      zret = Errata::Message(0, errno, "Failed to write stripe header ");
      ats_free(meta_t);
      return zret;
    }
  }
  ats_free(meta_t);
  return zret;
}

size_t
Stripe::vol_dirlen()
{
  return vol_headerlen() + ROUND_TO_STORE_BLOCK(((size_t)this->_buckets) * DIR_DEPTH * this->_segments * SIZEOF_DIR) +
         ROUND_TO_STORE_BLOCK(sizeof(StripeMeta));
}

void
Stripe::vol_init_data_internal()
{
  this->_buckets =
    ((this->_len.count() * 8192 - (this->_content - this->_start)) / cache_config_min_average_object_size) / DIR_DEPTH;
  this->_segments = (this->_buckets + (((1 << 16) - 1) / DIR_DEPTH)) / ((1 << 16) / DIR_DEPTH);
  this->_buckets  = (this->_buckets + this->_segments - 1) / this->_segments;
  this->_content  = this->_start + Bytes(2 * vol_dirlen());
}

void
Stripe::vol_init_data()
{
  // iteratively calculate start + buckets
  this->vol_init_data_internal();
  this->vol_init_data_internal();
  this->vol_init_data_internal();
}

void
Stripe::updateLiveData(enum Copy c)
{
  //  CacheStoreBlocks delta{_meta_pos[c][FOOT] - _meta_pos[c][HEAD]};
  CacheStoreBlocks header_len(0);
  //  int64_t n_buckets;
  //  int64_t n_segments;

  /*
   * COMMENTING THIS SECTION FOR NOW TO USE THE EXACT LOGIN USED IN ATS TO CALCULATE THE NUMBER OF SEGMENTS AND BUCKETS
  // Past the header is the segment free list heads which if sufficiently long (> ~4K) can take
  // more than 1 store block. Start with a guess of 1 and adjust upwards as needed. A 2TB stripe
  // with an AOS of 8000 has roughly 3700 segments meaning that for even 10TB drives this loop
  // should only be a few iterations.
  do {
    ++header_len;
    n_buckets  = Bytes(delta - header_len) / (sizeof(CacheDirEntry) * ts::ENTRIES_PER_BUCKET);
    n_segments = n_buckets / ts::MAX_BUCKETS_PER_SEGMENT;
    // This should never be more than one loop, usually none.
    while ((n_buckets / n_segments) > ts::MAX_BUCKETS_PER_SEGMENT)
      ++n_segments;
  } while ((sizeof(StripeMeta) + sizeof(uint16_t) * n_segments) > static_cast<size_t>(header_len));

  _buckets         = n_buckets / n_segments;
  _segments        = n_segments;
  */
  _directory._skip = header_len;
}

bool
dir_compare_tag(const CacheDirEntry *e, const CryptoHash *key)
{
  return (dir_tag(e) == DIR_MASK_TAG(key->slice32(2)));
}

int
vol_in_phase_valid(Stripe *d, CacheDirEntry *e)
{
  return (dir_offset(e) - 1 < ((d->_meta[0][0].write_pos + d->agg_buf_pos - d->_start) / CACHE_BLOCK_SIZE));
}

int
vol_out_of_phase_valid(Stripe *d, CacheDirEntry *e)
{
  return (dir_offset(e) - 1 >= ((d->_meta[0][0].agg_pos - d->_start) / CACHE_BLOCK_SIZE));
}

bool
Stripe::dir_valid(CacheDirEntry *_e)
{
  return (this->_meta[0][0].phase == dir_phase(_e) ? vol_in_phase_valid(this, _e) : vol_out_of_phase_valid(this, _e));
}

Bytes
Stripe::stripe_offset(CacheDirEntry *e)
{
  return this->_content + Bytes((dir_offset(e) * CACHE_BLOCK_SIZE) - CACHE_BLOCK_SIZE);
}

int
Stripe::dir_probe(CryptoHash *key, CacheDirEntry *result, CacheDirEntry **last_collision)
{
  int segment = key->slice32(0) % this->_segments;
  int bucket  = key->slice32(1) % this->_buckets;

  CacheDirEntry *seg = this->dir_segment(segment);
  CacheDirEntry *e   = nullptr;
  e                  = dir_bucket(bucket, seg);
  char *stripe_buff2 = nullptr;
  Doc *doc           = nullptr;
  // TODO: collision craft is pending.. look at the main ATS code. Assuming no collision for now
  if (dir_offset(e)) {
    do {
      if (dir_compare_tag(e, key)) {
        if (dir_valid(e)) {
          stripe_buff2 = (char *)ats_memalign(ats_pagesize(), dir_approx_size(e));
          std::cout << "dir_probe hit: found seg: " << segment << " bucket: " << bucket << " offset: " << dir_offset(e)
                    << "size: " << dir_approx_size(e) << std::endl;
          break;
        } else {
          // let's skip deleting for now
          // e = dir_delete_entry(e, p ,segment);
          // continue;
        }
      }
      e = next_dir(e, seg);

    } while (e);
    if (e == nullptr) {
      std::cout << "No directory entry found matching the URL key" << std::endl;
      return 0;
    }
    int fd       = _span->_fd;
    Bytes offset = stripe_offset(e);
    int64_t size = dir_approx_size(e);
    ssize_t n    = pread(fd, stripe_buff2, size, offset);
    if (n < size)
      std::cout << "Failed to read content from the Stripe:" << strerror(errno) << std::endl;

    doc = reinterpret_cast<Doc *>(stripe_buff2);
    std::string hdr(doc->hdr(), doc->hlen);

    std::string data_(doc->data(), doc->data_len());
    std::cout << "DATA\n" << data_ << std::endl;
  } else {
    std::cout << "Not found in the Cache" << std::endl;
  }
  free(stripe_buff2);
  return 0; // Why does this have a non-void return?
}

CacheDirEntry *
Stripe::dir_delete_entry(CacheDirEntry *e, CacheDirEntry *p, int s)
{
  CacheDirEntry *seg      = this->dir_segment(s);
  int no                  = dir_next(e);
  this->_meta[0][0].dirty = 1;
  if (p) {
    unsigned int fo = this->freelist[s];
    unsigned int eo = dir_to_offset(e, seg);
    dir_clear(e);
    dir_set_next(p, no);
    dir_set_next(e, fo);
    if (fo) {
      dir_set_prev(dir_from_offset(fo, seg), eo);
    }
    this->freelist[s] = eo;
  } else {
    CacheDirEntry *n = next_dir(e, seg);
    if (n) {
      dir_assign(e, n);
      dir_delete_entry(n, e, s);
      return e;
    } else {
      dir_clear(e);
      return nullptr;
    }
  }
  return dir_from_offset(no, seg);
}

void
Stripe::walk_all_buckets()
{
  for (int s = 0; s < this->_segments; s++) {
    if (walk_bucket_chain(s)) {
      std::cout << "Loop present in Segment " << s << std::endl;
    }
  }
}

bool
Stripe::walk_bucket_chain(int s)
{
  CacheDirEntry *seg = this->dir_segment(s);
  std::bitset<65536> b_bitset;
  b_bitset.reset();
  for (int b = 0; b < this->_buckets; b++) {
    CacheDirEntry *p = nullptr;
    auto *dir_b      = dir_bucket(b, seg);
    CacheDirEntry *e = dir_b;
    int len          = 0;

    while (e) {
      len++;
      int i = dir_to_offset(e, seg);
      if (b_bitset.test(i)) {
        std::cout << "bit already set in "
                  << "seg " << s << " bucket " << b << std::endl;
      }
      if (i > 0) { // i.e., not the first dir in the segment
        b_bitset[i] = true;
      }

#if 1
      if (!dir_valid(e) || !dir_offset(e)) {
        // std::cout<<"dir_clean in segment "<<s<<" =>cleaning "<<e<<" tag"<<dir_tag(e)<<" boffset"<< dir_offset(e)<< " bucket:
        // "<<dir_b<< " bucket len: "<<dir_bucket_length(dir_b, s)<<std::endl;
        e = dir_delete_entry(e, p, s);
        continue;
      }
#endif
      p = e;
      e = next_dir(e, seg);
    }
    //    std::cout<<"dir len in this bucket "<<len<<std::endl;
  }
  return false;
}

void
Stripe::dir_free_entry(CacheDirEntry *e, int s)
{
  CacheDirEntry *seg = this->dir_segment(s);
  unsigned int fo    = this->freelist[s];
  unsigned int eo    = dir_to_offset(e, seg);
  dir_set_next(e, fo);
  if (fo) {
    dir_set_prev(dir_from_offset(fo, seg), eo);
  }
  this->freelist[s] = eo;
}

// adds all the directory entries
// in a segment to the segment freelist
void
Stripe::dir_init_segment(int s)
{
  this->freelist[s]  = 0;
  CacheDirEntry *seg = this->dir_segment(s);
  int l, b;
  memset(seg, 0, SIZEOF_DIR * DIR_DEPTH * this->_buckets);
  for (l = 1; l < DIR_DEPTH; l++) {
    for (b = 0; b < this->_buckets; b++) {
      CacheDirEntry *bucket = dir_bucket(b, seg);
      this->dir_free_entry(dir_bucket_row(bucket, l), s);
    }
  }
}

void
Stripe::init_dir()
{
  for (int s = 0; s < this->_segments; s++) {
    this->freelist[s]  = 0;
    CacheDirEntry *seg = this->dir_segment(s);
    int l, b;
    for (l = 1; l < DIR_DEPTH; l++) {
      for (b = 0; b < this->_buckets; b++) {
        CacheDirEntry *bucket = dir_bucket(b, seg);
        this->dir_free_entry(dir_bucket_row(bucket, l), s);
        // std::cout<<"freelist"<<this->freelist[s]<<std::endl;
      }
    }
  }
}

Errata
Stripe::loadDir()
{
  Errata zret;
  int64_t dirlen = this->vol_dirlen();
  char *raw_dir  = (char *)ats_memalign(ats_pagesize(), dirlen);
  dir            = (CacheDirEntry *)(raw_dir + this->vol_headerlen());
  // read directory
  ssize_t n = pread(this->_span->_fd, raw_dir, dirlen, this->_start);
  if (n < dirlen) {
    std::cout << "Failed to read Dir from stripe @" << this->hashText;
  }
  return zret;
}
//
// Cache Directory
//

#if 0
// return value 1 means no loop
// zero indicates loop
int
dir_bucket_loop_check(CacheDirEntry *start_dir, CacheDirEntry *seg)
{
  if (start_dir == nullptr) {
    return 1;
  }

  CacheDirEntry *p1 = start_dir;
  CacheDirEntry *p2 = start_dir;

  while (p2) {
    // p1 moves by one entry per iteration
    assert(p1);
    p1 = next_dir(p1, seg);
    // p2 moves by two entries per iteration
    p2 = next_dir(p2, seg);
    if (p2) {
      p2 = next_dir(p2, seg);
    } else {
      return 1;
    }

    if (p2 == p1) {
      return 0; // we have a loop
    }
  }
  return 1;
}
#endif

int
Stripe::dir_freelist_length(int s)
{
  int free           = 0;
  CacheDirEntry *seg = this->dir_segment(s);
  CacheDirEntry *e   = dir_from_offset(this->freelist[s], seg);
  if (this->check_loop(s)) {
    return (DIR_DEPTH - 1) * this->_buckets;
  }
  while (e) {
    free++;
    e = next_dir(e, seg);
  }
  return free;
}

int
Stripe::check_loop(int s)
{
  // look for loop in the segment
  // rewrite the freelist if loop is present
  CacheDirEntry *seg = this->dir_segment(s);
  CacheDirEntry *e   = dir_from_offset(this->freelist[s], seg);
  std::bitset<65536> f_bitset;
  f_bitset.reset();
  while (e) {
    int i = dir_next(e);
    if (f_bitset.test(i)) {
      // bit was set in a previous round so a loop is present
      std::cout << "<check_loop> Loop present in Span" << this->_span->_path.string() << " Stripe: " << this->hashText
                << "Segment: " << s << std::endl;
      this->dir_init_segment(s);
      return 1;
    }
    f_bitset[i] = true;
    e           = dir_from_offset(i, seg);
  }

  return 0;
}

int
compare_ushort(void const *a, void const *b)
{
  return *static_cast<unsigned short const *>(a) - *static_cast<unsigned short const *>(b);
}

void
Stripe::dir_check()
{
  static int const SEGMENT_HISTOGRAM_WIDTH = 16;
  int hist[SEGMENT_HISTOGRAM_WIDTH + 1]    = {0};
  unsigned short chain_tag[MAX_ENTRIES_PER_SEGMENT];
  int32_t chain_mark[MAX_ENTRIES_PER_SEGMENT];

  this->loadMeta();
  this->loadDir();
  //  uint64_t total_buckets = _segments * _buckets;
  //  uint64_t total_entries = total_buckets * DIR_DEPTH;
  int frag_demographics[1 << DIR_SIZE_WIDTH][DIR_BLOCK_SIZES];
  int j;
  int stale = 0, in_use = 0, empty = 0;
  int free = 0, head = 0, buckets_in_use = 0;

  int max_chain_length = 0;
  int64_t bytes_in_use = 0;
  std::cout << "Stripe '[" << hashText << "]'" << std::endl;
  std::cout << "  Directory Bytes: " << _segments * _buckets * SIZEOF_DIR << std::endl;
  std::cout << "  Segments:  " << _segments << std::endl;
  std::cout << "  Buckets per segment:  " << _buckets << std::endl;
  std::cout << "  Entries:  " << _segments * _buckets * DIR_DEPTH << std::endl;
  for (int s = 0; s < _segments; s++) {
    CacheDirEntry *seg     = this->dir_segment(s);
    int seg_chain_max      = 0;
    int seg_empty          = 0;
    int seg_in_use         = 0;
    int seg_stale          = 0;
    int seg_bytes_in_use   = 0;
    int seg_dups           = 0;
    int seg_buckets_in_use = 0;

    ink_zero(chain_tag);
    memset(chain_mark, -1, sizeof(chain_mark));
    for (int b = 0; b < _buckets; b++) {
      CacheDirEntry *root = dir_bucket(b, seg);
      int h               = 0;
      int chain_idx       = 0;
      int mark            = 0;
      ++seg_buckets_in_use;
      // walking through the directories
      for (CacheDirEntry *e = root; e; e = next_dir(e, seg)) {
        if (!dir_offset(e)) {
          ++seg_empty;
          --seg_buckets_in_use;
          // this should only happen on the first dir in a bucket
          assert(nullptr == next_dir(e, seg));
          break;
        } else {
          int e_idx = e - seg;
          ++h;
          chain_tag[chain_idx++] = dir_tag(e);
          if (chain_mark[e_idx] == mark) {
            printf("    - Cycle of length %d detected for bucket %d\n", h, b);
          } else if (chain_mark[e_idx] >= 0) {
            printf("    - Entry %d is in chain %d and %d", e_idx, chain_mark[e_idx], mark);
          } else {
            chain_mark[e_idx] = mark;
          }

          if (!dir_valid(e)) {
            ++seg_stale;
          } else {
            uint64_t size = dir_approx_size(e);
            if (dir_head(e)) {
              ++head;
            }
            ++seg_in_use;
            seg_bytes_in_use += size;
            ++frag_demographics[dir_size(e)][dir_big(e)];
          }
        }
        e = next_dir(e, seg);
        if (!e) {
          break;
        }
      }

      // Check for duplicates (identical tags in the same bucket).
      if (h > 1) {
        unsigned short last;
        qsort(chain_tag, h, sizeof(chain_tag[0]), &compare_ushort);
        last = chain_tag[0];
        for (int k = 1; k < h; ++k) {
          if (last == chain_tag[k]) {
            ++seg_dups;
          }
          last = chain_tag[k];
        }
      }
      ++hist[std::min(h, SEGMENT_HISTOGRAM_WIDTH)];
      seg_chain_max = std::max(seg_chain_max, h);
    }
    int fl_size = dir_freelist_length(s);
    in_use += seg_in_use;
    empty += seg_empty;
    stale += seg_stale;
    free += fl_size;
    buckets_in_use += seg_buckets_in_use;
    max_chain_length = std::max(max_chain_length, seg_chain_max);
    bytes_in_use += seg_bytes_in_use;

    printf("  - Segment-%d | Entries: used=%d stale=%d free=%d disk-bytes=%d Buckets: used=%d empty=%d max=%d avg=%.2f dups=%d\n",
           s, seg_in_use, seg_stale, fl_size, seg_bytes_in_use, seg_buckets_in_use, seg_empty, seg_chain_max,
           seg_buckets_in_use ? static_cast<float>(seg_in_use + seg_stale) / seg_buckets_in_use : 0.0, seg_dups);
  }
  //////////////////

  printf("  - Stripe | Entries: in-use=%d stale=%d free=%d Buckets: empty=%d max=%d avg=%.2f\n", in_use, stale, free, empty,
         max_chain_length, buckets_in_use ? static_cast<float>(in_use + stale) / buckets_in_use : 0);

  printf("    Chain lengths:  ");
  for (j = 0; j < SEGMENT_HISTOGRAM_WIDTH; ++j) {
    printf(" %d=%d ", j, hist[j]);
  }
  printf(" %d>=%d\n", SEGMENT_HISTOGRAM_WIDTH, hist[SEGMENT_HISTOGRAM_WIDTH]);

  char tt[256];
  printf("    Total Size:      %" PRIu64 "\n", static_cast<uint64_t>(_len.count()));
  printf("    Bytes in Use:    %" PRIu64 " [%0.2f%%]\n", bytes_in_use, 100.0 * (static_cast<float>(bytes_in_use) / _len.count()));
  printf("    Objects:         %d\n", head);
  printf("    Average Size:    %" PRIu64 "\n", head ? (bytes_in_use / head) : 0);
  printf("    Average Frags:   %.2f\n", head ? static_cast<float>(in_use) / head : 0);
  printf("    Write Position:  %" PRIu64 "\n", _meta[0][0].write_pos - _content.count());
  printf("    Wrap Count:      %d\n", _meta[0][0].cycle);
  printf("    Phase:           %s\n", _meta[0][0].phase ? "true" : "false");
  ctime_r(&_meta[0][0].create_time, tt);
  tt[strlen(tt) - 1] = 0;
  printf("    Sync Serial:     %u\n", _meta[0][0].sync_serial);
  printf("    Write Serial:    %u\n", _meta[0][0].write_serial);
  printf("    Create Time:     %s\n", tt);
  printf("\n");
  printf("  Fragment size demographics\n");
  for (int b = 0; b < DIR_BLOCK_SIZES; ++b) {
    int block_size = DIR_BLOCK_SIZE(b);
    int s          = 0;
    while (s < 1 << DIR_SIZE_WIDTH) {
      for (int j = 0; j < 8; ++j, ++s) {
        // The size markings are redundant. Low values (less than DIR_SHIFT_WIDTH) for larger
        // base block sizes should never be used. Such entries should use the next smaller base block size.
        if (b > 0 && s < 1 << DIR_BLOCK_SHIFT(1)) {
          assert(frag_demographics[s][b] == 0);
          continue;
        }
        printf(" %8d[%2d:%1d]:%06d", (s + 1) * block_size, s, b, frag_demographics[s][b]);
      }
      printf("\n");
    }
  }
  printf("\n");
  ////////////////
}

Errata
Stripe::loadMeta()
{
  // Read from disk in chunks of this size. This needs to be a multiple of both the
  // store block size and the directory entry size so neither goes across read boundaries.
  // Beyond that the value should be in the ~10MB range for what I guess is best performance
  // vs. blocking production disk I/O on a live system.
  constexpr static int64_t N = (1 << 8) * CacheStoreBlocks::SCALE * sizeof(CacheDirEntry);

  Errata zret;

  int fd = _span->_fd;
  Bytes n;
  bool found;
  MemSpan data; // The current view of the read buffer.
  Bytes delta;
  Bytes pos = _start;
  // Avoid searching the entire span, because some of it must be content. Assume that AOS is more than 160
  // which means at most 10/160 (1/16) of the span can be directory/header.
  Bytes limit     = pos + _len / 16;
  size_t io_align = _span->_geometry.blocksz;
  StripeMeta const *meta;

  std::unique_ptr<char> bulk_buff;                      // Buffer for bulk reads.
  static const size_t SBSIZE = CacheStoreBlocks::SCALE; // save some typing.
  alignas(SBSIZE) char stripe_buff[SBSIZE];             // Use when reading a single stripe block.
  alignas(SBSIZE) char stripe_buff2[SBSIZE];            // use to save the stripe freelist
  if (io_align > SBSIZE) {
    return Errata::Message(0, 1, "Cannot load stripe ", _idx, " on span ", _span->_path.string(),
                           " because the I/O block alignment ", io_align, " is larger than the buffer alignment ", SBSIZE);
  }

  _directory._start = pos;
  // Header A must be at the start of the stripe block.
  // Todo: really need to check pread() for failure.
  ssize_t headerbyteCount = pread(fd, stripe_buff2, SBSIZE, pos);
  n.assign(headerbyteCount);
  data.assign(stripe_buff2, n);
  meta = data.ptr<StripeMeta>(0);
  // TODO:: We need to read more data at this point  to populate dir
  if (this->validateMeta(meta)) {
    delta              = Bytes(data.ptr<char>(0) - stripe_buff2);
    _meta[A][HEAD]     = *meta;
    _meta_pos[A][HEAD] = round_down(pos + Bytes(delta));
    pos += round_up(SBSIZE);
    _directory._skip = Bytes(SBSIZE); // first guess, updated in @c updateLiveData when the header length is computed.
    // Search for Footer A. Nothing for it except to grub through the disk.
    // The searched data is cached so it's available for directory parsing later if needed.
    while (pos < limit) {
      char *buff = static_cast<char *>(ats_memalign(io_align, N));
      bulk_buff.reset(buff);
      n.assign(pread(fd, buff, N, pos));
      data.assign(buff, n);
      found = this->probeMeta(data, &_meta[A][HEAD]);
      if (found) {
        ptrdiff_t diff     = data.ptr<char>(0) - buff;
        _meta[A][FOOT]     = data.template at<StripeMeta>(0);
        _meta_pos[A][FOOT] = round_down(pos + Bytes(diff));
        // don't bother attaching block if the footer is at the start
        if (diff > 0) {
          _directory._clip = Bytes(N - diff);
          _directory.append({bulk_buff.release(), N});
        }
        data += SBSIZE; // skip footer for checking on B copy.
        break;
      } else {
        _directory.append({bulk_buff.release(), N});
        pos += round_up(N);
      }
    }
  } else {
    zret.push(0, 1, "Header A not found");
  }
  pos = _meta_pos[A][FOOT];
  // Technically if Copy A is valid, Copy B is not needed. But at this point it's cheap to retrieve
  // (as the exact offset is computable).
  if (_meta_pos[A][FOOT] > 0) {
    delta = _meta_pos[A][FOOT] - _meta_pos[A][HEAD];
    // Header B should be immediately after Footer A. If at the end of the last read,
    // do another read.
    //    if (data.size() < CacheStoreBlocks::SCALE) {
    //      pos += round_up(N);
    //      n = Bytes(pread(fd, stripe_buff, CacheStoreBlocks::SCALE, pos));
    //      data.assign(stripe_buff, n);
    //    }
    pos  = this->_start + Bytes(vol_dirlen());
    meta = data.ptr<StripeMeta>(0);
    if (this->validateMeta(meta)) {
      _meta[B][HEAD]     = *meta;
      _meta_pos[B][HEAD] = round_down(pos);

      // Footer B must be at the same relative offset to Header B as Footer A -> Header A.
      pos += delta;
      n = Bytes(pread(fd, stripe_buff, ts::CacheStoreBlocks::SCALE, pos));
      data.assign(stripe_buff, n);
      meta = data.ptr<StripeMeta>(0);
      if (this->validateMeta(meta)) {
        _meta[B][FOOT]     = *meta;
        _meta_pos[B][FOOT] = round_down(pos);
      }
    }
  }

  if (_meta_pos[A][FOOT] > 0) {
    if (_meta[A][HEAD].sync_serial == _meta[A][FOOT].sync_serial &&
        (0 == _meta_pos[B][FOOT] || _meta[B][HEAD].sync_serial != _meta[B][FOOT].sync_serial ||
         _meta[A][HEAD].sync_serial >= _meta[B][HEAD].sync_serial)) {
      this->updateLiveData(A);
    } else if (_meta_pos[B][FOOT] > 0 && _meta[B][HEAD].sync_serial == _meta[B][FOOT].sync_serial) {
      this->updateLiveData(B);
    } else {
      zret.push(0, 1, "Invalid stripe data - candidates found but sync serial data not valid. ", _meta[A][HEAD].sync_serial, ":",
                _meta[A][FOOT].sync_serial, ":", _meta[B][HEAD].sync_serial, ":", _meta[B][FOOT].sync_serial);
    }
  }

  n.assign(headerbyteCount);
  data.assign(stripe_buff2, n);
  meta = data.ptr<StripeMeta>(0);
  // copy freelist
  freelist = (uint16_t *)malloc(_segments * sizeof(uint16_t));
  for (int i = 0; i < _segments; i++) {
    freelist[i] = meta->freelist[i];
  }

  if (!zret) {
    _directory.clear();
  }
  return zret;
}

} // namespace ct
