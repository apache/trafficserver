/** @file

  Transforms content using gzip or deflate

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

#include "configuration.h"
#include <fstream>
#include <algorithm>
#include <vector>
#include <fnmatch.h>

namespace Gzip
{
using namespace std;

void
ltrim_if(string &s, int (*fp)(int))
{
  for (size_t i = 0; i < s.size();) {
    if (fp(s[i])) {
      s.erase(i, 1);
    } else {
      break;
    }
  }
}

void
rtrim_if(string &s, int (*fp)(int))
{
  for (ssize_t i = (ssize_t)s.size() - 1; i >= 0; i--) {
    if (fp(s[i])) {
      s.erase(i, 1);
    } else {
      break;
    }
  }
}

void
trim_if(string &s, int (*fp)(int))
{
  ltrim_if(s, fp);
  rtrim_if(s, fp);
}

vector<string>
tokenize(const string &s, int (*fp)(int))
{
  vector<string> r;
  string tmp;

  for (size_t i = 0; i < s.size(); i++) {
    if (fp(s[i])) {
      if (tmp.size()) {
        r.push_back(tmp);
        tmp = "";
      }
    } else {
      tmp += s[i];
    }
  }

  if (tmp.size()) {
    r.push_back(tmp);
  }

  return r;
}

enum ParserState {
  kParseStart,
  kParseCompressibleContentType,
  kParseRemoveAcceptEncoding,
  kParseEnable,
  kParseCache,
  kParseDisallow,
};

void
Configuration::AddHostConfiguration(HostConfiguration *hc)
{
  host_configurations_.push_back(hc);
}

void
HostConfiguration::add_disallow(const std::string &disallow)
{
  disallows_.push_back(disallow);
}

void
HostConfiguration::add_compressible_content_type(const std::string &content_type)
{
  compressible_content_types_.push_back(content_type);
}

HostConfiguration *
Configuration::Find(const char *host, int host_length)
{
  HostConfiguration *host_configuration = host_configurations_[0];

  std::string shost(host, host_length);

  for (size_t i = 1; i < host_configurations_.size(); i++) {
    if (host_configurations_[i]->host() == shost) {
      host_configuration = host_configurations_[i];
      break;
    }
  }

  return host_configuration;
}

bool
HostConfiguration::IsUrlAllowed(const char *url, int url_len)
{
  string surl(url, url_len);

  for (size_t i = 0; i < disallows_.size(); i++) {
    if (fnmatch(disallows_[i].c_str(), surl.c_str(), 0) == 0) {
      info("url [%s] disabled for compression, matched on pattern [%s]", surl.c_str(), disallows_[i].c_str());
      return false;
    }
  }

  return true;
}

bool
HostConfiguration::ContentTypeIsCompressible(const char *content_type, int content_type_length)
{
  string scontent_type(content_type, content_type_length);
  bool is_match = false;

  for (size_t i = 0; i < compressible_content_types_.size(); i++) {
    const char *match_string = compressible_content_types_[i].c_str();
    bool exclude             = match_string[0] == '!';
    if (exclude) {
      match_string++; // skip '!'
    }
    if (fnmatch(match_string, scontent_type.c_str(), 0) == 0) {
      info("compressible content type [%s], matched on pattern [%s]", scontent_type.c_str(),
           compressible_content_types_[i].c_str());
      is_match = !exclude;
    }
  }

  return is_match;
}

Configuration *
Configuration::Parse(const char *path)
{
  string pathstring(path);

  // If we have a path and it's not an absolute path, make it relative to the
  // configuration directory.
  if (!pathstring.empty() && pathstring[0] != '/') {
    pathstring.assign(TSConfigDirGet());
    pathstring.append("/");
    pathstring.append(path);
  }

  trim_if(pathstring, isspace);

  Configuration *c                              = new Configuration();
  HostConfiguration *current_host_configuration = new HostConfiguration("");
  c->AddHostConfiguration(current_host_configuration);
  current_host_configuration->add_compressible_content_type("text/*");
  current_host_configuration->add_compressible_content_type("application/xml*");
  current_host_configuration->add_compressible_content_type("*javascript*");
  current_host_configuration->add_compressible_content_type("image/svg+xml");

  if (pathstring.empty()) {
    return c;
  }

  path = pathstring.c_str();
  info("Parsing file \"%s\"", path);
  std::ifstream f;

  size_t lineno = 0;

  f.open(path, std::ios::in);

  if (!f.is_open()) {
    warning("could not open file [%s], skip", path);
    return c;
  }

  enum ParserState state = kParseStart;

  while (!f.eof()) {
    std::string line;
    getline(f, line);
    ++lineno;

    trim_if(line, isspace);
    if (line.size() == 0) {
      continue;
    }

    vector<string> v = tokenize(line, isspace);

    for (size_t i = 0; i < v.size(); i++) {
      string token = v[i];
      trim_if(token, isspace);

      // should not happen
      if (!token.size())
        continue;

      // once a comment is encountered, we are done processing the line
      if (token[0] == '#')
        break;

      switch (state) {
      case kParseStart:
        if ((token[0] == '[') && (token[token.size() - 1] == ']')) {
          std::string current_host   = token.substr(1, token.size() - 2);
          current_host_configuration = new HostConfiguration(current_host);
          c->AddHostConfiguration(current_host_configuration);
        } else if (token == "compressible-content-type") {
          state = kParseCompressibleContentType;
        } else if (token == "remove-accept-encoding") {
          state = kParseRemoveAcceptEncoding;
        } else if (token == "enabled") {
          state = kParseEnable;
        } else if (token == "cache") {
          state = kParseCache;
        } else if (token == "disallow") {
          state = kParseDisallow;
        } else {
          warning("failed to interpret \"%s\" at line %zu", token.c_str(), lineno);
        }
        break;
      case kParseCompressibleContentType:
        current_host_configuration->add_compressible_content_type(token);
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
      case kParseDisallow:
        current_host_configuration->add_disallow(token);
        state = kParseStart;
        break;
      }
    }
  }

  if (state != kParseStart) {
    warning("the parser state indicates that data was expected when it reached the end of the file (%d)", state);
  }

  return c;
} // Configuration::Parse
} // namespace
