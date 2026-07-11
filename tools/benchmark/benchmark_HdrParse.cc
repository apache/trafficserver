/** @file

  Micro-benchmark for HTTP header parsing (src/proxy/hdrs).

  Two modes in one binary:

    * A/B stats mode (default): Catch2 BENCHMARK cases produce mean/median/
      stddev per (target x corpus) so an optimization can be measured before
      and after and guarded against regression. Run e.g.:
        benchmark_HdrParse "[bench]" --benchmark-samples 100

    * Profiling mode (--profile <target>): a tight fixed-count loop with no
      Catch2 harness overhead, meant to be wrapped by perf/vtune:
        perf stat -e cycles,instructions \
          benchmark_HdrParse --profile request --iters 5000000

  Targets: request, response, mime, url, wks.

  A realistic + adversarial corpus is built in. Real captured header blocks can
  be supplied with --corpus-file FILE or --corpus-dir DIR (blocks split on a
  blank line, classified request vs response by the first line); these are added
  to both modes.

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

#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/URL.h"
#include "proxy/hdrs/HdrToken.h"
#include "proxy/hdrs/HdrHeap.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

// Defined in tscore; disables thread-local proxy allocators so no event-system
// thread setup is required (mirrors the hdrs unit-test main).
extern int cmd_disable_pfreelist;

namespace
{
// ----------------------------------------------------------------------------
// Corpus
// ----------------------------------------------------------------------------

struct HeaderCase {
  std::string label;
  std::string data; // full block including the start line, ends "\r\n\r\n"
  bool        is_response = false;
};

struct Corpus {
  std::vector<HeaderCase>  cases;
  std::vector<std::string> urls; // bare request targets for the url target
  std::vector<std::string> wks;  // field names (owned) for the wks target
};

// A representative modern browser request and typical responses, alongside the
// classic fixtures used by the hdrs unit tests.
constexpr std::string_view REQ_REALISTIC =
  "GET /assets/app.9f2c.js HTTP/1.1\r\n"
  "Host: www.example.com\r\n"
  "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
  "Chrome/126.0.0.0 Safari/537.36\r\n"
  "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
  "Accept-Encoding: gzip, deflate, br, zstd\r\n"
  "Accept-Language: en-US,en;q=0.9\r\n"
  "Cookie: session=8f14e45fceea167a5a36dedd4bea2543; theme=dark; region=us-west-2; ab_bucket=37\r\n"
  "Referer: https://www.example.com/\r\n"
  "Connection: keep-alive\r\n"
  "\r\n";

constexpr std::string_view REQ_CLASSIC = "GET http://www.news.com:80/ HTTP/1.0\r\n"
                                         "Proxy-Connection: Keep-Alive\r\n"
                                         "User-Agent: Mozilla/4.04 [en] (X11; I; Linux 2.0.33 i586)\r\n"
                                         "Pragma: no-cache\r\n"
                                         "Host: www.news.com\r\n"
                                         "Accept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\n"
                                         "Accept-Language: en\r\n"
                                         "Accept-Charset: iso-8859-1, *, utf-8\r\n"
                                         "\r\n";

constexpr std::string_view RESP_REALISTIC = "HTTP/1.1 200 OK\r\n"
                                            "Server: ATS/10.1.0\r\n"
                                            "Date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                                            "Content-Type: text/html; charset=utf-8\r\n"
                                            "Content-Length: 12345\r\n"
                                            "Cache-Control: max-age=31536000, public, immutable\r\n"
                                            "Vary: Accept-Encoding\r\n"
                                            "Age: 42\r\n"
                                            "\r\n";

constexpr std::string_view RESP_304 = "HTTP/1.1 304 Not Modified\r\n"
                                      "Date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                                      "Etag: \"6f2c9a1b\"\r\n"
                                      "Cache-Control: max-age=31536000\r\n"
                                      "\r\n";

constexpr std::string_view URL_REALISTIC = "http://www.example.com/images/2026/06/some-article/hero.webp?w=1200&q=75";

// Build the adversarial cases programmatically so their worst-case sizes are
// obvious and easy to tune.
std::string
gen_many_fields(int n)
{
  std::string s = "GET /many HTTP/1.1\r\nHost: h\r\n";
  for (int i = 0; i < n; ++i) {
    s += "X-Custom-Header-" + std::to_string(i) + ": value-" + std::to_string(i) + "\r\n";
  }
  s += "\r\n";
  return s;
}

std::string
gen_long_value(int len)
{
  std::string s = "GET /long HTTP/1.1\r\nHost: h\r\nX-Blob: ";
  s.append(len, 'a');
  s += "\r\n\r\n";
  return s;
}

// Field names that are guaranteed not to be well-known, forcing the
// hdrtoken_tokenize hash miss + per-char field-name validation path.
std::string
gen_wks_miss(int n)
{
  std::string s = "GET /miss HTTP/1.1\r\nHost: h\r\n";
  for (int i = 0; i < n; ++i) {
    s += "X-Zzq-Nonstandard-Field-" + std::to_string(i) + ": v\r\n";
  }
  s += "\r\n";
  return s;
}

// Many duplicates of a well-known, commonly-repeated field: stresses
// mime_hdr_field_attach duplicate chaining.
std::string
gen_dup_fields(int n)
{
  std::string s = "HTTP/1.1 200 OK\r\nDate: Mon, 21 Oct 2013 20:13:21 GMT\r\n";
  for (int i = 0; i < n; ++i) {
    s += "Set-Cookie: c" + std::to_string(i) + "=v" + std::to_string(i) + "; Path=/; HttpOnly\r\n";
  }
  s += "\r\n";
  return s;
}

std::string
gen_long_uri(int len)
{
  std::string s = "GET http://www.example.com/";
  s.append(len, 'x');
  s += " HTTP/1.1\r\nHost: www.example.com\r\n\r\n";
  return s;
}

// The MIME-only target parses a field block with the start line removed.
std::string_view
strip_start_line(std::string_view block)
{
  auto pos = block.find('\n');
  return pos == std::string_view::npos ? block : block.substr(pos + 1);
}

// Collect the field names in a block (for the wks target). Skips the start line,
// continuation lines, and lines without a colon.
void
collect_field_names(std::string_view block, std::vector<std::string> &out)
{
  std::string_view rest = strip_start_line(block);
  size_t           pos  = 0;
  while (pos < rest.size()) {
    size_t           eol  = rest.find('\n', pos);
    std::string_view line = rest.substr(pos, (eol == std::string_view::npos ? rest.size() : eol) - pos);
    pos                   = (eol == std::string_view::npos) ? rest.size() : eol + 1;
    if (line.empty() || line == "\r" || line.front() == ' ' || line.front() == '\t') {
      continue; // blank terminator or folded continuation
    }
    size_t colon = line.find(':');
    if (colon != std::string_view::npos && colon > 0) {
      out.emplace_back(line.substr(0, colon));
    }
  }
}

// Load raw header blocks from a file. Multiple blocks may be separated by a
// blank line. Line endings are normalized and each block re-terminated with a
// canonical "\r\n\r\n".
void
load_file(const std::filesystem::path &path, std::vector<HeaderCase> &out)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "warning: cannot open corpus file %s\n", path.c_str());
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string content = ss.str();

  // Normalize CRLF -> LF, then split blocks on a blank line ("\n\n").
  std::string norm;
  norm.reserve(content.size());
  for (char ch : content) {
    if (ch != '\r') {
      norm += ch;
    }
  }

  int    idx = 0;
  size_t pos = 0;
  while (pos < norm.size()) {
    size_t           sep   = norm.find("\n\n", pos);
    size_t           end   = (sep == std::string::npos) ? norm.size() : sep;
    std::string_view chunk = std::string_view(norm).substr(pos, end - pos);
    // Trim leading/trailing newlines.
    while (!chunk.empty() && chunk.front() == '\n') {
      chunk.remove_prefix(1);
    }
    while (!chunk.empty() && chunk.back() == '\n') {
      chunk.remove_suffix(1);
    }
    if (!chunk.empty()) {
      // Re-insert canonical CRLF line endings and terminator.
      std::string block;
      for (size_t i = 0; i < chunk.size(); ++i) {
        if (chunk[i] == '\n') {
          block += "\r\n";
        } else {
          block += chunk[i];
        }
      }
      block += "\r\n\r\n";
      HeaderCase c;
      c.label       = path.filename().string() + "#" + std::to_string(idx++);
      c.is_response = block.rfind("HTTP/", 0) == 0; // status line starts with "HTTP/"
      c.data        = std::move(block);
      out.push_back(std::move(c));
    }
    if (sep == std::string::npos) {
      break;
    }
    pos = sep + 2;
  }
}

Corpus
build_corpus(const std::vector<std::filesystem::path> &files, const std::vector<std::filesystem::path> &dirs)
{
  Corpus corp;
  auto   add = [&](std::string_view label, std::string_view data, bool is_resp) {
    corp.cases.push_back({std::string(label), std::string(data), is_resp});
  };

  // Realistic.
  add("req_realistic", REQ_REALISTIC, false);
  add("req_classic", REQ_CLASSIC, false);
  add("resp_realistic", RESP_REALISTIC, true);
  add("resp_304", RESP_304, true);

  // Adversarial.
  add("adv_many_fields", gen_many_fields(100), false);
  add("adv_long_value", gen_long_value(8192), false);
  add("adv_wks_miss", gen_wks_miss(50), false);
  add("adv_dup_fields", gen_dup_fields(50), true);
  add("adv_long_uri", gen_long_uri(4000), false);

  // File-loaded.
  for (const auto &f : files) {
    load_file(f, corp.cases);
  }
  for (const auto &d : dirs) {
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(d, ec); !ec && it != std::filesystem::directory_iterator(); ++it) {
      if (it->is_regular_file()) {
        load_file(it->path(), corp.cases);
      }
    }
  }

  corp.urls = {std::string(URL_REALISTIC), "http://www.example.com/" + std::string(4000, 'x')};

  for (const auto &c : corp.cases) {
    collect_field_names(c.data, corp.wks);
  }

  return corp;
}

Corpus g_corpus;

// ----------------------------------------------------------------------------
// Parse drivers. Each does the minimal realistic setup, one parse, teardown, and
// returns {ParseResult, sink}.
//
// The primary production path is ZERO-COPY: the IOBuffer proxy path
// (HdrTSOnly.cc parse_req(IOBufferReader*)) attaches the socket block to the
// heap and parses with must_copy_strings=false, and it runs under
// strict_uri_parsing=2 (the config default). So the drivers default to
// copy=false and strict=2; pass copy=true to measure the copy path. With
// copy=false the parsed field pointers alias the input buffer, which is a stable
// static corpus string here, so this is safe for the lifetime of the parse.
// ----------------------------------------------------------------------------

constexpr bool PROD_COPY   = false; // zero-copy IOBuffer proxy path
constexpr int  PROD_STRICT = 2;     // proxy.config.http.strict_uri_parsing default

using ParseOutcome = std::pair<ParseResult, uint64_t>;

ParseOutcome
drive_request(std::string_view raw, bool copy = PROD_COPY, int strict = PROD_STRICT)
{
  HTTPParser parser;
  http_parser_init(&parser);
  HTTPHdr  hdr;
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64); // +64 avoids proxy alloc
  hdr.create(HTTPType::REQUEST, HTTP_1_1, heap);
  const char *start = raw.data();
  ParseResult ret   = http_parser_parse_req(&parser, hdr.m_heap, hdr.m_http, &start, raw.data() + raw.size(), copy,
                                            /*eof*/ true, strict, UINT16_MAX, 131070);
  uint64_t    sink  = static_cast<uint64_t>(start - raw.data());
  hdr.destroy();
  return {ret, sink};
}

ParseOutcome
drive_response(std::string_view raw, bool copy = PROD_COPY)
{
  HTTPParser parser;
  http_parser_init(&parser);
  HTTPHdr  hdr;
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  hdr.create(HTTPType::RESPONSE, HTTP_1_1, heap);
  const char *start = raw.data();
  ParseResult ret   = http_parser_parse_resp(&parser, hdr.m_heap, hdr.m_http, &start, raw.data() + raw.size(), copy, /*eof*/ true);
  uint64_t    sink  = static_cast<uint64_t>(start - raw.data());
  hdr.destroy();
  return {ret, sink};
}

ParseOutcome
drive_mime(std::string_view fields, bool copy = PROD_COPY)
{
  MIMEParser parser;
  mime_parser_init(&parser);
  MIMEHdr  hdr;
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  hdr.create(heap);
  const char *start = fields.data();
  ParseResult ret   = mime_parser_parse(&parser, hdr.m_heap, hdr.m_mime, &start, fields.data() + fields.size(), copy,
                                        /*eof*/ true, /*remove_ws_from_field_name*/ false);
  uint64_t    sink  = static_cast<uint64_t>(start - fields.data());
  hdr.destroy();
  return {ret, sink};
}

ParseOutcome
drive_url(std::string_view uri, bool copy = PROD_COPY, int strict = PROD_STRICT)
{
  URL      url;
  HdrHeap *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  url.create(heap);
  const char *start = uri.data();
  ParseResult ret   = url_parse(heap, url.m_url_impl, &start, uri.data() + uri.size(), copy, strict, /*verify_host*/ true);
  // Read back parsed state so a successful parse (ParseResult::DONE == 0) cannot
  // be optimized away.
  uint64_t sink = static_cast<uint64_t>(ret) + static_cast<uint64_t>(url.host_get().length()) + url.port_get();
  url.destroy();
  return {ret, sink};
}

// Isolates the per-field well-known-string hash + lookup (and the miss-path
// validation) without the surrounding MIME machinery.
uint64_t
drive_wks(const std::vector<std::string> &names)
{
  uint64_t sink = 0;
  for (const auto &n : names) {
    const char *wks  = nullptr;
    int         idx  = hdrtoken_tokenize(n.data(), static_cast<int>(n.size()), &wks);
    sink            += static_cast<uint64_t>(idx + 1) + reinterpret_cast<uintptr_t>(wks);
  }
  return sink;
}

// ----------------------------------------------------------------------------
// Profiling mode
// ----------------------------------------------------------------------------

enum class Target { Request, Response, Mime, Url, Wks, Unknown };

Target
parse_target(std::string_view s)
{
  if (s == "request") {
    return Target::Request;
  }
  if (s == "response") {
    return Target::Response;
  }
  if (s == "mime") {
    return Target::Mime;
  }
  if (s == "url") {
    return Target::Url;
  }
  if (s == "wks") {
    return Target::Wks;
  }
  return Target::Unknown;
}

const char *
profile_target_name(Target t)
{
  switch (t) {
  case Target::Request:
    return "request";
  case Target::Response:
    return "response";
  case Target::Mime:
    return "mime";
  case Target::Url:
    return "url";
  case Target::Wks:
    return "wks";
  default:
    return "unknown";
  }
}

int
run_profile(Target target, uint64_t iters)
{
  // Assemble the input set and a per-iteration byte count for throughput.
  std::vector<std::string_view> inputs;
  uint64_t                      bytes_per_pass = 0;

  auto add_input = [&](std::string_view v) {
    inputs.push_back(v);
    bytes_per_pass += v.size();
  };

  switch (target) {
  case Target::Request:
    for (const auto &c : g_corpus.cases) {
      if (!c.is_response) {
        add_input(c.data);
      }
    }
    break;
  case Target::Response:
    for (const auto &c : g_corpus.cases) {
      if (c.is_response) {
        add_input(c.data);
      }
    }
    break;
  case Target::Mime:
    for (const auto &c : g_corpus.cases) {
      add_input(strip_start_line(c.data));
    }
    break;
  case Target::Url:
    for (const auto &u : g_corpus.urls) {
      add_input(u);
    }
    break;
  case Target::Wks:
    // Handled below (uses the owned name list directly).
    for (const auto &n : g_corpus.wks) {
      bytes_per_pass += n.size();
    }
    break;
  default:
    std::fprintf(stderr, "unknown --profile target\n");
    return 2;
  }

  if (target != Target::Wks && inputs.empty()) {
    std::fprintf(stderr, "no inputs for the requested target\n");
    return 2;
  }

  volatile uint64_t sink  = 0;
  auto              t0    = std::chrono::steady_clock::now();
  uint64_t          count = 0;

  if (target == Target::Wks) {
    for (uint64_t i = 0; i < iters; ++i) {
      sink += drive_wks(g_corpus.wks);
    }
    count = iters; // one full pass over all names per iter
  } else {
    for (uint64_t i = 0; i < iters; ++i) {
      std::string_view in = inputs[i % inputs.size()];
      ParseOutcome     r;
      switch (target) {
      case Target::Request:
        r = drive_request(in);
        break;
      case Target::Response:
        r = drive_response(in);
        break;
      case Target::Mime:
        r = drive_mime(in);
        break;
      case Target::Url:
        r = drive_url(in);
        break;
      default:
        break;
      }
      sink += r.second;
    }
    count = iters;
  }

  auto   t1     = std::chrono::steady_clock::now();
  double ns     = std::chrono::duration<double, std::nano>(t1 - t0).count();
  double ns_per = ns / static_cast<double>(count);

  // One "op" is one parse for the parse targets, or one full pass over the name
  // list for wks. Throughput is over the header bytes actually processed.
  double avg_in      = inputs.empty() ? 0.0 : static_cast<double>(bytes_per_pass) / static_cast<double>(inputs.size());
  double total_bytes = (target == Target::Wks) ? static_cast<double>(bytes_per_pass) * static_cast<double>(iters) :
                                                 avg_in * static_cast<double>(iters);
  double mibps       = (total_bytes / (ns / 1e9)) / (1024.0 * 1024.0);

  std::printf("target=%s iters=%llu  %.2f ns/op  %.2f Mops/s  %.0f MiB/s  sink=%llu\n", profile_target_name(target),
              static_cast<unsigned long long>(count), ns_per, 1000.0 / ns_per, mibps, static_cast<unsigned long long>(sink));
  return 0;
}

} // namespace

// ----------------------------------------------------------------------------
// A/B stats mode (Catch2 BENCHMARK)
// ----------------------------------------------------------------------------

namespace
{
const HeaderCase &
find_case(std::string_view label)
{
  for (const auto &c : g_corpus.cases) {
    if (c.label == label) {
      return c;
    }
  }
  FAIL("missing corpus case: " << label);
  return g_corpus.cases.front();
}
} // namespace

// Built-in cases must fully parse (DONE + all bytes consumed); file-loaded cases
// (label carries a '#') only must not ERROR.
bool
is_file_case(const HeaderCase &c)
{
  return c.label.find('#') != std::string::npos;
}

TEST_CASE("hdr parse: request", "[bench][request]")
{
  for (const auto &c : g_corpus.cases) {
    if (c.is_response) {
      continue;
    }
    CAPTURE(c.label);
    auto [ret, consumed] = drive_request(c.data);
    if (is_file_case(c)) {
      REQUIRE(ret != ParseResult::ERROR);
    } else {
      REQUIRE(ret == ParseResult::DONE);
      REQUIRE(consumed == c.data.size());
    }
  }

  const auto &realistic = find_case("req_realistic");
  const auto &many      = find_case("adv_many_fields");

  BENCHMARK("request: realistic (zero-copy)")
  {
    return drive_request(realistic.data).second;
  };
  BENCHMARK("request: realistic (copy)")
  {
    return drive_request(realistic.data, /*copy*/ true).second;
  };
  BENCHMARK("request: 100 fields")
  {
    return drive_request(many.data).second;
  };
}

TEST_CASE("hdr parse: response", "[bench][response]")
{
  for (const auto &c : g_corpus.cases) {
    if (!c.is_response) {
      continue;
    }
    CAPTURE(c.label);
    auto [ret, consumed] = drive_response(c.data);
    if (is_file_case(c)) {
      REQUIRE(ret != ParseResult::ERROR);
    } else {
      REQUIRE(ret == ParseResult::DONE);
      REQUIRE(consumed == c.data.size());
    }
  }

  const auto &realistic = find_case("resp_realistic");
  const auto &dups      = find_case("adv_dup_fields");

  BENCHMARK("response: realistic")
  {
    return drive_response(realistic.data).second;
  };
  BENCHMARK("response: 50 dup fields")
  {
    return drive_response(dups.data).second;
  };
}

TEST_CASE("hdr parse: mime only", "[bench][mime]")
{
  const auto &realistic = find_case("req_realistic");
  const auto &wksmiss   = find_case("adv_wks_miss");

  BENCHMARK("mime: realistic fields")
  {
    return drive_mime(strip_start_line(realistic.data)).second;
  };
  BENCHMARK("mime: 50 wks-miss fields")
  {
    return drive_mime(strip_start_line(wksmiss.data)).second;
  };
}

TEST_CASE("hdr parse: url only", "[bench][url]")
{
  REQUIRE(drive_url(URL_REALISTIC).first != ParseResult::ERROR);

  BENCHMARK("url: realistic")
  {
    return drive_url(URL_REALISTIC).second;
  };
}

TEST_CASE("hdr parse: wks tokenize", "[bench][wks]")
{
  REQUIRE(!g_corpus.wks.empty());

  BENCHMARK("wks: tokenize all field names")
  {
    return drive_wks(g_corpus.wks);
  };
}

// ----------------------------------------------------------------------------
// Entry point (own main, like the hdrs unit-test stub, so we can init the WKS
// tables and branch to the profiling loop).
// ----------------------------------------------------------------------------

int
main(int argc, char *argv[])
{
  // No thread setup, forbid thread-local allocators (mirrors unit_test_main.cc).
  cmd_disable_pfreelist = true;
  // Populate the HTTP well-known strings; parsing depends on them.
  http_init();

  // Pull out our own flags (--profile/--iters/--corpus-*) and pass the rest to
  // Catch. --corpus-* feed both modes.
  std::vector<std::filesystem::path> corpus_files, corpus_dirs;
  std::string                        profile_target;
  uint64_t                           iters = 5'000'000;
  std::vector<char *>                catch_args;
  catch_args.push_back(argv[0]);

  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    if (a == "--profile" && i + 1 < argc) {
      profile_target = argv[++i];
    } else if (a == "--iters" && i + 1 < argc) {
      iters = std::strtoull(argv[++i], nullptr, 10);
    } else if (a == "--corpus-file" && i + 1 < argc) {
      corpus_files.emplace_back(argv[++i]);
    } else if (a == "--corpus-dir" && i + 1 < argc) {
      corpus_dirs.emplace_back(argv[++i]);
    } else {
      catch_args.push_back(argv[i]);
    }
  }

  g_corpus = build_corpus(corpus_files, corpus_dirs);

  if (!profile_target.empty()) {
    Target t = parse_target(profile_target);
    if (t == Target::Unknown) {
      std::fprintf(stderr, "unknown target '%s' (want: request|response|mime|url|wks)\n", profile_target.c_str());
      return 2;
    }
    return run_profile(t, iters);
  }

  return Catch::Session().run(static_cast<int>(catch_args.size()), catch_args.data());
}
