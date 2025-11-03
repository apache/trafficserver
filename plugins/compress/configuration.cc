/** @file

  Transforms content using gzip, deflate or brotli

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

#include "tscore/ink_config.h"
#include "configuration.h"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <fnmatch.h>
#include <system_error>
#include <stdexcept>

#include "swoc/swoc_file.h"
#include "swoc/TextView.h"
#include "swoc/Lexicon.h"

#include "debug_macros.h"

#include <cctype>

namespace Gzip
{
swoc::TextView
extractFirstToken(swoc::TextView &view, int (*fp)(int))
{
  // Skip leading delimiters
  view.ltrim_if(fp);

  // Extract token up to (and removing) the first delimiter
  auto token = view.take_prefix_if(fp);

  return token;
}

enum class ParserState {
  Start,
  CompressibleContentType,
  RemoveAcceptEncoding,
  Enable,
  Cache,
  RangeRequest,
  Flush,
  Allow,
  MinimumContentLength,
  ContentTypeIgnoreParameters,
  GzipCompressionLevel,
  BrotliCompressionLevel,
  BrotliLGWSize,
  ZstdCompressionLevel
};

void
Configuration::add_host_configuration(HostConfiguration *hc)
{
  host_configurations_.emplace_back(hc);
}

void
HostConfiguration::update_defaults()
{
  // maintain backwards compatibility/usability out of the box
  if (compressible_status_codes_.empty()) {
    compressible_status_codes_ = {TS_HTTP_STATUS_OK, TS_HTTP_STATUS_PARTIAL_CONTENT, TS_HTTP_STATUS_NOT_MODIFIED};
  }
}

void
HostConfiguration::add_allow(swoc::TextView allow)
{
  allows_.emplace_back(allow);
}

void
HostConfiguration::add_compressible_content_type(swoc::TextView content_type)
{
  compressible_content_types_.emplace_back(content_type);
}

HostConfiguration *
Configuration::find(const char *host, int host_length)
{
  HostConfiguration *host_configuration = host_configurations_[0];

  if (host && host_length > 0 && host_configurations_.size() > 1) {
    swoc::TextView host_view(host, host_length);

    // Start from index 1 to skip the default configuration at index 0
    for (size_t i = 1; i < host_configurations_.size(); ++i) {
      if (host_configurations_[i]->host() == host_view) {
        host_configuration = host_configurations_[i];
        break;
      }
    }
  }

  return host_configuration;
}

bool
HostConfiguration::is_url_allowed(const char *url, int url_len)
{
  swoc::TextView url_view(url, url_len);

  if (has_allows()) {
    // fnmatch requires null-terminated strings, so we need a std::string for the url
    std::string surl(url_view);
    for (const auto &allow : allows_) {
      const char *match_string = allow.c_str();
      bool        exclude      = allow.starts_with('!');
      if (exclude) {
        ++match_string; // skip !
      }
      if (fnmatch(match_string, surl.c_str(), 0) == 0) {
        info("url [%.*s] %s for compression, matched allow pattern [%s]", static_cast<int>(url_view.size()), url_view.data(),
             exclude ? "disabled" : "enabled", allow.c_str());
        return !exclude;
      }
    }
    info("url [%.*s] disabled for compression, did not match any allows pattern", static_cast<int>(url_view.size()),
         url_view.data());
    return false;
  }
  info("url [%.*s] enabled for compression, did not match any pattern", static_cast<int>(url_view.size()), url_view.data());
  return true;
}

bool
HostConfiguration::is_status_code_compressible(const TSHttpStatus status_code) const
{
  return compressible_status_codes_.contains(status_code);
}

swoc::TextView
strip_params(swoc::TextView v)
{
  v = v.take_prefix_at(';');
  v.rtrim_if(&::isspace);
  return v;
}

bool
HostConfiguration::is_content_type_compressible(const char *content_type, int content_type_length)
{
  swoc::TextView content_type_view(content_type, content_type_length);
  bool           is_match = false;

  for (const auto &content_type_pattern : compressible_content_types_) {
    const char *match_string = content_type_pattern.c_str();
    bool        exclude      = content_type_pattern.starts_with('!');

    if (exclude) {
      ++match_string; // skip '!'
    }
    std::string target;

    if (content_type_ignore_parameters() && content_type_pattern.find(';') == std::string::npos) {
      target = strip_params(content_type_view);
    } else {
      target = content_type_view;
    }
    if (fnmatch(match_string, target.c_str(), 0) == 0) {
      info("compressible content type [%s], matched on pattern [%s]", target.c_str(), content_type_pattern.c_str());
      is_match = !exclude;
    }
  }

  return is_match;
}

constexpr int
isCommaOrSpace(int ch)
{
  return (ch == ',') or isspace(ch);
}

void
HostConfiguration::add_compression_algorithms(swoc::TextView line)
{
  compression_algorithms_ = ALGORITHM_DEFAULT; // remove the default gzip.
  for (;;) {
    auto token = extractFirstToken(line, isCommaOrSpace);
    if (token.empty()) {
      break;
    } else if (token == "zstd") {
#ifdef HAVE_ZSTD_H
      compression_algorithms_ |= ALGORITHM_ZSTD;
#else
      error("supported-algorithms: zstd support not compiled in.");
#endif
    } else if (token == "br") {
#ifdef HAVE_BROTLI_ENCODE_H
      compression_algorithms_ |= ALGORITHM_BROTLI;
#else
      error("supported-algorithms: brotli support not compiled in.");
#endif
    } else if (token == "gzip") {
      compression_algorithms_ |= ALGORITHM_GZIP;
    } else if (token == "deflate") {
      compression_algorithms_ |= ALGORITHM_DEFLATE;
    } else {
#ifdef HAVE_ZSTD_H
      error("Unknown compression type. Supported compression-algorithms <zstd,br,gzip,deflate>.");
#else
      error("Unknown compression type. Supported compression-algorithms <br,gzip,deflate>.");
#endif
    }
  }
}

void
HostConfiguration::add_compressible_status_codes(swoc::TextView line)
{
  compressible_status_codes_.clear();

  for (;;) {
    auto token = extractFirstToken(line, isCommaOrSpace);
    if (token.empty()) {
      break;
    }

    swoc::TextView parsed;
    uintmax_t      status_code = swoc::svtou(token, &parsed);
    if (parsed.size() == token.size() && status_code > 0) {
      compressible_status_codes_.insert(static_cast<TSHttpStatus>(status_code));
    } else {
      error("Invalid status code %.*s", static_cast<int>(token.size()), token.data());
    }
  }
}

int
HostConfiguration::compression_algorithms()
{
  return compression_algorithms_;
}

/**
  "true" and "false" are compatibility with old version, will be removed
 */

// Lexicon for mapping range-request configuration tokens to enum values
static const swoc::Lexicon<RangeRequestCtrl> RangeRequestLexicon{
  swoc::Lexicon<RangeRequestCtrl>::with_multi{{RangeRequestCtrl::NONE, {"true", "none"}},
                                              {RangeRequestCtrl::NO_COMPRESSION, {"false", "no-compression"}},
                                              {RangeRequestCtrl::REMOVE_RANGE, {"remove-range"}},
                                              {RangeRequestCtrl::REMOVE_ACCEPT_ENCODING, {"remove-accept-encoding"}}}
};

static const std::unordered_map<std::string_view, ParserState> KeywordToStateMap{
  {"compressible-content-type",      ParserState::CompressibleContentType    },
  {"content_type_ignore_parameters", ParserState::ContentTypeIgnoreParameters},
  {"remove-accept-encoding",         ParserState::RemoveAcceptEncoding       },
  {"enabled",                        ParserState::Enable                     },
  {"cache",                          ParserState::Cache                      },
  {"range-request",                  ParserState::RangeRequest               },
  {"flush",                          ParserState::Flush                      },
  {"allow",                          ParserState::Allow                      },
  {"minimum-content-length",         ParserState::MinimumContentLength       },
  {"gzip-compression-level",         ParserState::GzipCompressionLevel       },
  {"brotli-compression-level",       ParserState::BrotliCompressionLevel     },
  {"brotli-lgwin",                   ParserState::BrotliLGWSize              },
  {"zstd-compression-level",         ParserState::ZstdCompressionLevel       }
};

void
HostConfiguration::set_range_request(swoc::TextView token)
{
  try {
    range_request_ctl_ = RangeRequestLexicon[token];
  } catch (std::domain_error const &) {
    error("invalid token for range_request: %.*s", static_cast<int>(token.size()), token.data());
  }
}

Configuration *
Configuration::Parse(const char *path)
{
  swoc::file::path pathstring(path);

  // If we have a path and it's not an absolute path, make it relative to the
  // configuration directory.
  if (!pathstring.is_absolute()) {
    pathstring = swoc::file::path(TSConfigDirGet()) / pathstring;
  }

  Configuration     *c                          = new Configuration();
  HostConfiguration *current_host_configuration = new HostConfiguration("");

  c->add_host_configuration(current_host_configuration);

  if (pathstring.empty()) {
    return c;
  }

  auto path_string = pathstring.c_str();
  info("Parsing file \"%s\"", path_string);

  std::error_code ec;
  std::string     content = swoc::file::load(pathstring, ec);

  if (ec) {
    warning("could not open file [%s], skip: %s", path_string, ec.message().c_str());
    return c;
  }

  ParserState state  = ParserState::Start;
  size_t      lineno = 0;

  swoc::TextView content_view(content);
  while (content_view) {
    auto line_view = content_view.take_prefix_at('\n');
    ++lineno;

    // Trim whitespace
    line_view.trim_if(&::isspace);
    if (line_view.empty()) {
      continue;
    }
    for (;;) {
      auto token = extractFirstToken(line_view, isspace);

      if (token.empty()) {
        break;
      }

      // once a comment is encountered, we are done processing the line
      if (token.starts_with('#')) {
        break;
      }

      switch (state) {
      case ParserState::Start:
        if (token.starts_with('[') && token.ends_with(']')) {
          auto host_name = token.substr(1, token.size() - 2);

          // Makes sure that any default settings are properly set, when not explicitly set via configs
          current_host_configuration->update_defaults();
          current_host_configuration = new HostConfiguration(host_name);
          c->add_host_configuration(current_host_configuration);
        } else if (token == "supported-algorithms") {
          current_host_configuration->add_compression_algorithms(line_view);
          state = ParserState::Start;
        } else if (token == "compressible-status-code") {
          current_host_configuration->add_compressible_status_codes(line_view);
          state = ParserState::Start;
        } else if (auto it = KeywordToStateMap.find(std::string_view(token.data(), token.size())); it != KeywordToStateMap.end()) {
          state = it->second;
        } else {
          warning("failed to interpret \"%.*s\" at line %zu", static_cast<int>(token.size()), token.data(), lineno);
        }
        break;
      case ParserState::CompressibleContentType:
        current_host_configuration->add_compressible_content_type(token);
        state = ParserState::Start;
        break;
      case ParserState::ContentTypeIgnoreParameters:
        current_host_configuration->set_content_type_ignore_parameters(token == "true");
        state = ParserState::Start;
        break;
      case ParserState::RemoveAcceptEncoding:
        current_host_configuration->set_remove_accept_encoding(token == "true");
        state = ParserState::Start;
        break;
      case ParserState::Enable:
        current_host_configuration->set_enabled(token == "true");
        state = ParserState::Start;
        break;
      case ParserState::Cache:
        current_host_configuration->set_cache(token == "true");
        state = ParserState::Start;
        break;
      case ParserState::RangeRequest:
        current_host_configuration->set_range_request(token);
        state = ParserState::Start;
        break;
      case ParserState::Flush:
        current_host_configuration->set_flush(token == "true");
        state = ParserState::Start;
        break;
      case ParserState::Allow:
        current_host_configuration->add_allow(token);
        state = ParserState::Start;
        break;
      case ParserState::MinimumContentLength: {
        swoc::TextView parsed;
        uintmax_t      length = swoc::svtou(token, &parsed);
        if (parsed.size() == token.size()) {
          current_host_configuration->set_minimum_content_length(length);
        }
        state = ParserState::Start;
        break;
      }
      case ParserState::GzipCompressionLevel: {
        swoc::TextView parsed;
        intmax_t       level = swoc::svtoi(token, &parsed);
        if (parsed.size() == token.size() && level >= 1 && level <= 9) {
          current_host_configuration->set_gzip_compression_level(level);
        } else {
          error("gzip-compression-level must be between 1 and 9, got %.*s", static_cast<int>(token.size()), token.data());
        }
        state = ParserState::Start;
        break;
      }
      case ParserState::BrotliCompressionLevel: {
        swoc::TextView parsed;
        intmax_t       level = swoc::svtoi(token, &parsed);
        if (parsed.size() == token.size() && level >= 0 && level <= 11) {
          current_host_configuration->set_brotli_compression_level(level);
        } else {
          error("brotli-compression-level must be between 0 and 11, got %.*s", static_cast<int>(token.size()), token.data());
        }
        state = ParserState::Start;
        break;
      }
      case ParserState::BrotliLGWSize: {
        swoc::TextView parsed;
        intmax_t       lgw = swoc::svtoi(token, &parsed);
        if (parsed.size() == token.size() && lgw >= 10 && lgw <= 24) {
          current_host_configuration->set_brotli_lgw_size(lgw);
        } else {
          error("brotli-lgwin must be between 10 and 24, got %.*s", static_cast<int>(token.size()), token.data());
        }
        state = ParserState::Start;
        break;
      }
      case ParserState::ZstdCompressionLevel: {
        swoc::TextView parsed;
        intmax_t       level = swoc::svtoi(token, &parsed);
        if (parsed.size() == token.size() && level >= 1 && level <= 22) {
          current_host_configuration->set_zstd_compression_level(level);
        } else {
          error("zstd-compression-level must be between 1 and 22, got %.*s", static_cast<int>(token.size()), token.data());
        }
        state = ParserState::Start;
        break;
      }
      }
    }
  }

  // Update the defaults for the last host configuration too, if needed.
  current_host_configuration->update_defaults();

  // Check combination of configs
  if (!current_host_configuration->cache() && current_host_configuration->range_request_ctl() == RangeRequestCtrl::NONE) {
    warning("Combination of 'cache false' and 'range-request none' might deliver corrupted content");
  }

  if (state != ParserState::Start) {
    warning("the parser state indicates that data was expected when it reached the end of the file (%d)", static_cast<int>(state));
  }

  return c;
} // Configuration::Parse
} // namespace Gzip
