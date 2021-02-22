/** @file

  SSL client certificate verification plugin, utility source file.

  Checks for specificate names in the client provided certificate and
  fails the handshake if none of the good names are present

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

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <yaml-cpp/yaml.h>

#include "client_allow_list.h"

#if defined(CLIENT_ALLOW_LIST_UNIT_TEST)

#include <cstdarg>

extern void ut_printf(char const *fmt, std::va_list args);

namespace
{
char const *
TSConfigDirGet()
{
  return ".";
}

void
TSError(char const *fmt, ...)
{
  std::va_list args;

  va_start(args, fmt);
  ut_printf(fmt, args);
  va_end(args);
}

[[noreturn]] void
TSEmergency(char const *fmt, ...)
{
  std::va_list args;

  va_start(args, fmt);
  ut_printf(fmt, args);
  va_end(args);

  throw ClientAllowListUTException();
}

} // end anonymous namespace

#undef TSAssert
#define TSAssert(EXPR) ((EXPR) ? static_cast<void>(0) : TSEmergency("Assert Failed line=%d", __LINE__))

#endif // defined(CLIENT_ALLOW_LIST_UNIT_TEST)

namespace
{
[[noreturn]] void
bad_node(std::string const &filespec, YAML::Node const &node)
{
  TSEmergency(PN ": config error: file=%s line=%d column=%d", filespec.c_str(), node.Mark().line + 1, node.Mark().column + 1);

  throw 0; // Never executed, but avoids compiler warning that this function returns.
}

} // end anonymous namespace

namespace client_allow_list_plugin
{
std::vector<cname_matcher> matcher;

std::vector<unsigned> other_matcher_idxs;

std::vector<unsigned> none_matcher_idxs;

MapCStrToUVec::~MapCStrToUVec()
{
  char const *key;
  auto it = _map.begin();
  while (it != _map.end()) {
    key = it->first;
    _map.erase(it);
    delete[] key;
    it = _map.begin();
  }
}

MapCStrToUVec sname_to_matcher_idxs;

bool
check_name(std::vector<unsigned> const &matcher_idxs, std::string_view name)
{
  bool name_matched{false};
  int strvec[30];
  for (unsigned idx : matcher_idxs) {
    auto &a_matcher = matcher[idx];
    if (a_matcher._compiled_RE == nullptr) {
      if (std::string_view(a_matcher._CName) == name) {
        name_matched = true;
        break;
      }
    } else {
      int exec_res{pcre_exec(a_matcher._compiled_RE, nullptr, name.data(), name.size(), 0, 0, strvec, 30)};
      if (1 == exec_res) {
        name_matched = true;
        break;

      } else if (exec_res != PCRE_ERROR_NOMATCH) {
        TSError(PN ": bad result from pcre_exec=%d for name=%.*s, matcher named %s, idx=%u", exec_res,
                static_cast<int>(name.size()), name.data(), a_matcher._CName.c_str(), idx);
      }
    }
  }
  return name_matched;
}

void
Init::_yaml_process(char const *config_filespec)
{
  std::string filespec;
  if (config_filespec[0] != '/') {
    char const *config_dir = TSConfigDirGet();

    if (config_dir) {
      filespec = std::string(config_dir) + '/';
    }
  }

  filespec += config_filespec;

  try {
    YAML::Node config = YAML::LoadFile(filespec);

    bool none_seen{false}, other_seen{false};

    for (YAML::const_iterator it = config.begin(); it != config.end(); ++it) {
      std::string key = it->first.as<std::string>();

      std::vector<std::vector<unsigned> *> list_of_matcher_idx_vectors;
      std::string_view sname;
      if (key.size() == 0) {
        TSError(PN ": empty server name list");
        bad_node(filespec, it->first);
      }
      if (std::string::npos != key.find_first_of(std::string_view(" \t\n"))) {
        TSError(PN ": blank space not allowed in server name list");
        bad_node(filespec, it->first);
      }
      std::size_t pos{0}, end_pos;
      while (pos < key.size()) {
        end_pos = key.find_first_of(std::string_view("|,"), pos);
        if (std::string::npos == end_pos) {
          end_pos = key.size();
        } else if (end_pos == pos) {
          TSError(PN ": empty server name in server name list");
          bad_node(filespec, it->first);
        }
        sname = std::string_view(key).substr(pos, end_pos - pos);
        pos   = end_pos + 1;
        if ("<none>" == sname) {
          if (none_seen) {
            TSError(PN ": <none> used more than once");
            bad_node(filespec, it->first);
          }
          none_seen = true;
          list_of_matcher_idx_vectors.push_back(&none_matcher_idxs);

        } else if ("<other>" == sname) {
          if (other_seen) {
            TSError(PN ": <other> used more than once");
            bad_node(filespec, it->first);
          }
          other_seen = true;
          list_of_matcher_idx_vectors.push_back(&other_matcher_idxs);

        } else {
          auto vp = sname_to_matcher_idxs.add(sname);
          if (!vp) {
            TSError(PN ": cert names for SNI server name \"%.*s\" previously specified", static_cast<int>(sname.size()),
                    sname.data());
            bad_node(filespec, it->first);
          }
          list_of_matcher_idx_vectors.push_back(vp);
        }
      }
      TSAssert(list_of_matcher_idx_vectors.size() > 0);

      _Populator pop(this);
      if (it->second.IsSequence()) {
        for (YAML::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
          if (!pop.add_cert_name(it2->as<std::string>().c_str())) {
            bad_node(filespec, *it2);
          }
        }

      } else {
        // Value is scalar, single cert name.
        if (!pop.add_cert_name(it->second.as<std::string>().c_str())) {
          bad_node(filespec, it->second);
        }
      }
      *list_of_matcher_idx_vectors[0] = std::move(pop.matcher_idxs);

      // If there is more than one SNI server name, make a copy of the vector of matcher indexes for each
      // additional one.
      //
      for (std::size_t i = 1; i < list_of_matcher_idx_vectors.size(); ++i) {
        *list_of_matcher_idx_vectors[i] = *list_of_matcher_idx_vectors[0];
      }
    }
    if (sname_to_matcher_idxs.size() == 0) {
      TSEmergency(PN ": YAML config file %s is empty", filespec.c_str());
    }
  } catch (const YAML::Exception &e) {
    TSEmergency(PN ": YAML::Exception \"%s\" when parsing YAML config file %s", e.what(), filespec.c_str());
  }
}

void
Init::operator()(int n_args, char const *const *arg)
{
  if (n_args < 2) {
    TSEmergency(PN ": must provide at least one plugin parameter");
  }
  if (2 == n_args) {
    std::string_view first_arg(arg[1]);
    if ((first_arg.size() > 5) && (first_arg.substr(first_arg.size() - 5) == ".yaml")) {
      // Single parameter that is a yaml conifg file.
      //
      _yaml_process(arg[1]);
      return;
    }
  }
  // Otherwise the arguments are a list of name patterns that all client certs must match.
  //
  _process_name_args(n_args - 1, arg + 1);
}

void
Init::_process_name_args(int n_names, char const *const *name)
{
  _Populator populator(this);

  for (int i = 0; i < n_names; i++) {
    if (!populator.add_cert_name(name[i])) {
      TSEmergency(PN ": fatal error");
    }
  }
  other_matcher_idxs = std::move(populator.matcher_idxs);
  none_matcher_idxs  = other_matcher_idxs;
}

bool
Init::_Populator::add_cert_name(std::string_view name)
{
  if (name.size() == 0) {
    // Empty name, match nothing.
    return true;
  }
  if (std::count(std::begin(name), std::end(name), '*') > 1) {
    TSError(PN ": bad certificate name pattern %.*s", static_cast<int>(name.size()), name.data());
    return false;
  }
  unsigned name_idx;
  auto it = _init->_name_to_idx_map.find(name);
  if (it != _init->_name_to_idx_map.end()) {
    name_idx = it->second;
    if (_idx_present_flag[name_idx]) {
      TSError(PN ": duplicate name pattern %.*s", static_cast<int>(name.size()), name.data());
      return false;
    }
  } else {
    // New name pattern, so new matcher.

    name_idx = matcher.size();
    matcher.emplace_back();
    matcher[name_idx]._CName = std::string(name.data(), name.size());

    if (name.find("*") != std::string::npos) {
      // Name pattern has wildcard, so translate it to a PCRE and compile it.

      std::size_t si = 0, di = 1;
      _init->_insert_tmp[0] = '^';

      while (si < name.size()) {
        if (_init->_insert_tmp.size() < (di + 8)) {
          _init->_insert_tmp.resize(di + 128);
        }
        if ('.' == name[si]) {
          std::memcpy(_init->_insert_tmp.data() + di, "\\.", 3);
          di += 2;
        } else if ('*' == name[si]) {
          std::memcpy(_init->_insert_tmp.data() + di, ".{0,}", 5);
          di += 5;
        } else {
          _init->_insert_tmp[di++] = name[si];
        }
        ++si;
      }
      _init->_insert_tmp[di++] = '$';
      _init->_insert_tmp[di]   = '\0';

      const char *err_ptr = "<unknown>";
      int err_offset      = 0;
      char const *pattern = _init->_insert_tmp.data();
      pcre *ptr_cre{pcre_compile(pattern, 0, &err_ptr, &err_offset, nullptr)};
      if (nullptr == ptr_cre) {
        TSError(PN ": PCRE could not compile pattern %s, error at offset %d, error is: %s", _init->_insert_tmp.data(), err_offset,
                err_ptr);
        return false;
      }
      matcher[name_idx]._compiled_RE = ptr_cre;
    }
    _init->_name_to_idx_map.emplace(name, name_idx);

    TSAssert(name_idx == _idx_present_flag.size());
    _idx_present_flag.resize(name_idx + 1);
  }
  _idx_present_flag[name_idx] = true;

  matcher_idxs.push_back(name_idx);

  return true;
}

} // namespace client_allow_list_plugin
