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
#include <vector>
#include <fnmatch.h>
#include <system_error>

#include "swoc/swoc_file.h"
#include "swoc/TextView.h"

#include "debug_macros.h"

#include <cctype>

namespace Gzip
{
using namespace std;

swoc::TextView
extractFirstToken(swoc::TextView &view, int (*fp)(int))
{
  // Skip leading delimiters
  view.ltrim_if([fp](char c) -> bool { return fp(static_cast<unsigned char>(c)) != 0; });

  // Extract token up to (and removing) the first delimiter
  auto token = view.take_prefix_if([fp](char c) -> bool { return fp(static_cast<unsigned char>(c)) != 0; });

  return token;
}

enum ParserState {
  kParseStart,
  kParseCompressibleContentType,
  kParseRemoveAcceptEncoding,
  kParseEnable,
  kParseCache,
  kParseRangeRequest,
  kParseFlush,
  kParseAllow,
  kParseMinimumContentLength,
  kParseContentTypeIgnoreParameters
};

void
Configuration::add_host_configuration(HostConfiguration *hc)
{
  host_configurations_.push_back(hc);
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
HostConfiguration::add_allow(const std::string &allow)
{
  allows_.push_back(allow);
}

void
HostConfiguration::add_compressible_content_type(const std::string &content_type)
{
  compressible_content_types_.push_back(content_type);
}

HostConfiguration *
Configuration::find(const char *host, int host_length)
{
  HostConfiguration *host_configuration = host_configurations_[0];

  if (host && host_length > 0 && host_configurations_.size() > 1) {
    std::string shost(host, host_length);

    // ToDo: Maybe use std::find() here somehow?
    for (HostContainer::iterator it = host_configurations_.begin() + 1; it != host_configurations_.end(); ++it) {
      if ((*it)->host() == shost) {
        host_configuration = *it;
        break;
      }
    }
  }

  return host_configuration;
}

bool
HostConfiguration::is_url_allowed(const char *url, int url_len)
{
  string surl(url, url_len);
  if (has_allows()) {
    for (StringContainer::iterator allow_it = allows_.begin(); allow_it != allows_.end(); ++allow_it) {
      const char *match_string = allow_it->c_str();
      bool        exclude      = match_string[0] == '!';
      if (exclude) {
        ++match_string; // skip !
      }
      if (fnmatch(match_string, surl.c_str(), 0) == 0) {
        info("url [%s] %s for compression, matched allow pattern [%s]", surl.c_str(), exclude ? "disabled" : "enabled",
             allow_it->c_str());
        return !exclude;
      }
    }
    info("url [%s] disabled for compression, did not match any allows pattern", surl.c_str());
    return false;
  }
  info("url [%s] enabled for compression, did not match any pattern", surl.c_str());
  return true;
}

bool
HostConfiguration::is_status_code_compressible(const TSHttpStatus status_code) const
{
  std::set<TSHttpStatus>::const_iterator it = compressible_status_codes_.find(status_code);

  return it != compressible_status_codes_.end();
}

std::string_view
strip_params(std::string_view v)
{
  swoc::TextView tv{v};
  tv = tv.take_prefix_at(';');
  tv.rtrim_if(&::isspace);
  return tv;
}

bool
HostConfiguration::is_content_type_compressible(const char *content_type, int content_type_length)
{
  string scontent_type(content_type, content_type_length);
  bool   is_match = false;

  for (StringContainer::iterator it = compressible_content_types_.begin(); it != compressible_content_types_.end(); ++it) {
    const char *match_string = it->c_str();
    if (match_string == nullptr) {
      continue;
    }
    bool exclude = match_string[0] == '!';

    if (exclude) {
      ++match_string; // skip '!'
    }
    std::string target;
    if (content_type_ignore_parameters() && std::strchr(match_string, ';') == nullptr) {
      target = strip_params(std::string_view(scontent_type));
    } else {
      target = scontent_type;
    }
    if (fnmatch(match_string, target.c_str(), 0) == 0) {
      info("compressible content type [%s], matched on pattern [%s]", target.c_str(), it->c_str());
      is_match = !exclude;
    }
  }

  return is_match;
}

int
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
      error("Unknown compression type. Supported compression-algorithms <br,gzip,deflate>.");
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
void
HostConfiguration::set_range_request(swoc::TextView token)
{
  if (token == "true" || token == "none") {
    range_request_ctl_ = RangeRequestCtrl::NONE;
  } else if (token == "false" || token == "no-compression") {
    range_request_ctl_ = RangeRequestCtrl::NO_COMPRESSION;
  } else if (token == "remove-range") {
    range_request_ctl_ = RangeRequestCtrl::REMOVE_RANGE;
  } else if (token == "remove-accept-encoding") {
    range_request_ctl_ = RangeRequestCtrl::REMOVE_ACCEPT_ENCODING;
  } else {
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

  enum ParserState state  = kParseStart;
  size_t           lineno = 0;

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
      if (token[0] == '#') {
        break;
      }

      switch (state) {
      case kParseStart:
        if ((token[0] == '[') && (token[token.size() - 1] == ']')) {
          std::string current_host(token.substr(1, token.size() - 2));

          // Makes sure that any default settings are properly set, when not explicitly set via configs
          current_host_configuration->update_defaults();
          current_host_configuration = new HostConfiguration(current_host);
          c->add_host_configuration(current_host_configuration);
        } else if (token == "compressible-content-type") {
          state = kParseCompressibleContentType;
        } else if (token == "content_type_ignore_parameters") {
          state = kParseContentTypeIgnoreParameters;
        } else if (token == "remove-accept-encoding") {
          state = kParseRemoveAcceptEncoding;
        } else if (token == "enabled") {
          state = kParseEnable;
        } else if (token == "cache") {
          state = kParseCache;
        } else if (token == "range-request") {
          state = kParseRangeRequest;
        } else if (token == "flush") {
          state = kParseFlush;
        } else if (token == "supported-algorithms") {
          current_host_configuration->add_compression_algorithms(line_view);
          state = kParseStart;
        } else if (token == "allow") {
          state = kParseAllow;
        } else if (token == "compressible-status-code") {
          current_host_configuration->add_compressible_status_codes(line_view);
          state = kParseStart;
        } else if (token == "minimum-content-length") {
          state = kParseMinimumContentLength;
        } else {
          warning("failed to interpret \"%.*s\" at line %zu", static_cast<int>(token.size()), token.data(), lineno);
        }
        break;
      case kParseCompressibleContentType:
        current_host_configuration->add_compressible_content_type(std::string(token));
        state = kParseStart;
        break;
      case kParseContentTypeIgnoreParameters:
        current_host_configuration->set_content_type_ignore_parameters(token == "true");
        state = kParseStart;
        break;
      case kParseRemoveAcceptEncoding:
        current_host_configuration->set_remove_accept_encoding(token == "true");
        state = kParseStart;
        break;
      case kParseEnable:
        current_host_configuration->set_enabled(token == "true");
        state = kParseStart;
        break;
      case kParseCache:
        current_host_configuration->set_cache(token == "true");
        state = kParseStart;
        break;
      case kParseRangeRequest:
        current_host_configuration->set_range_request(token);
        state = kParseStart;
        break;
      case kParseFlush:
        current_host_configuration->set_flush(token == "true");
        state = kParseStart;
        break;
      case kParseAllow:
        current_host_configuration->add_allow(std::string(token));
        state = kParseStart;
        break;
      case kParseMinimumContentLength: {
        swoc::TextView parsed;
        uintmax_t      length = swoc::svtou(token, &parsed);
        if (parsed.size() == token.size()) {
          current_host_configuration->set_minimum_content_length(length);
        }
        state = kParseStart;
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

  if (state != kParseStart) {
    warning("the parser state indicates that data was expected when it reached the end of the file (%d)", state);
  }

  return c;
} // Configuration::Parse
} // namespace Gzip
