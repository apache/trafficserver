/*
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

#include "mmdb.h"

///////////////////////////////////////////////////////////////////////////////
// Load the config file from param
// check for basics
// Clear out any existing data since this may be a reload
bool
Acl::init(char const *filename)
{
  struct stat s;
  bool status = false;

  YAML::Node maxmind;

  configloc.clear();

  if (filename[0] != '/') {
    // relative file
    configloc = TSConfigDirGet();
    configloc += "/";
    configloc.append(filename);
  } else {
    configloc.assign(filename);
  }

  if (stat(configloc.c_str(), &s) < 0) {
    TSDebug(PLUGIN_NAME, "Could not stat %s", configloc.c_str());
    return status;
  }

  try {
    _config = YAML::LoadFile(configloc.c_str());

    if (_config.IsNull()) {
      TSDebug(PLUGIN_NAME, "Config file not found or unreadable");
      return status;
    }
    if (!_config["maxmind"]) {
      TSDebug(PLUGIN_NAME, "Config file not in maxmind namespace");
      return status;
    }

    // Get our root maxmind node
    maxmind = _config["maxmind"];
#if 0
    // Test junk
    for (YAML::const_iterator it = maxmind.begin(); it != maxmind.end(); ++it) {
      const std::string &name    = it->first.as<std::string>();
      YAML::NodeType::value type = it->second.Type();
      TSDebug(PLUGIN_NAME, "name: %s, value: %d", name.c_str(), type);
    }
#endif
  } catch (const YAML::Exception &e) {
    TSError("[%s] YAML::Exception %s when parsing YAML config file %s for maxmind", PLUGIN_NAME, e.what(), configloc.c_str());
    return status;
  }

  // Associate our config file with remap.config to be able to initiate reloads
  TSMgmtString result;
  const char *var_name = "proxy.config.url_remap.filename";
  TSMgmtStringGet(var_name, &result);
  TSMgmtConfigFileAdd(result, configloc.c_str());

  // Find our database name and convert to full path as needed
  status = loaddb(maxmind["database"]);
  if (!status) {
    TSDebug(PLUGIN_NAME, "Failed to load MaxMind Database");
    return status;
  }

  // Clear out existing data, these may no longer exist in a new config and so we
  // dont want old ones left behind
  allow_country.clear();
  allow_ip_map.clear();
  deny_ip_map.clear();
  allow_regex_path.clear();
  deny_regex_path.clear();
  allow_regex_host.clear();
  deny_regex_host.clear();
  _html.clear();
  default_allow       = false;
  _anonymous_blocking = false;
  _anonymous_ip       = false;
  _anonymous_vpn      = false;
  _hosting_provider   = false;
  _tor_exit_node      = false;
  _residential_proxy  = false;
  _public_proxy       = false;

  if (loadallow(maxmind["allow"])) {
    TSDebug(PLUGIN_NAME, "Loaded Allow ruleset");
    status = true;
  } else {
    // We have no proper allow ruleset
    // setting to allow by default to only apply deny rules
    default_allow = true;
  }

  if (loaddeny(maxmind["deny"])) {
    TSDebug(PLUGIN_NAME, "Loaded Deny ruleset");
    status = true;
  }

  loadhtml(maxmind["html"]);

  _anonymous_blocking = loadanonymous(maxmind["anonymous"]);

  if (!status) {
    TSDebug(PLUGIN_NAME, "Failed to load any rulesets, none specified");
    status = false;
  }

  return status;
}

///////////////////////////////////////////////////////////////////////////////
// Parse the anonymous blocking settings
bool
Acl::loadanonymous(const YAML::Node &anonNode)
{
  if (!anonNode) {
    TSDebug(PLUGIN_NAME, "No anonymous rules set");
    return false;
  }
  if (anonNode.IsNull()) {
    TSDebug(PLUGIN_NAME, "Anonymous rules are NULL");
    return false;
  }

#if 0
  // Test junk
  for (YAML::const_iterator it = anonNode.begin(); it != anonNode.end(); ++it) {
    const std::string &name    = it->first.as<std::string>();
    YAML::NodeType::value type = it->second.Type();
    TSDebug(PLUGIN_NAME, "name: %s, value: %d", name.c_str(), type);
  }
#endif

  try {
    if (anonNode["ip"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw ip true");
      _anonymous_ip = true;
    }

    if (anonNode["vpn"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw vpn true");
      _anonymous_vpn = true;
    }

    if (anonNode["hosting"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw hosting true");
      _hosting_provider = true;
    }

    if (anonNode["public"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw public proxy true");
      _public_proxy = true;
    }

    if (anonNode["tor"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw tor exit node true");
      _tor_exit_node = true;
    }

    if (anonNode["residential"].as<bool>(false)) {
      TSDebug(PLUGIN_NAME, "saw residential proxy true");
      _residential_proxy = true;
    }

  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file anonymous list", e.what());
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Parse the deny list country codes and IPs
bool
Acl::loaddeny(const YAML::Node &denyNode)
{
  if (!denyNode) {
    TSDebug(PLUGIN_NAME, "No Deny rules set");
    return false;
  }
  if (denyNode.IsNull()) {
    TSDebug(PLUGIN_NAME, "Deny rules are NULL");
    return false;
  }

#if 0
  // Test junk
  for (YAML::const_iterator it = denyNode.begin(); it != denyNode.end(); ++it) {
    const std::string &name    = it->first.as<std::string>();
    YAML::NodeType::value type = it->second.Type();
    TSDebug(PLUGIN_NAME, "name: %s, value: %d", name.c_str(), type);
  }
#endif

  // Load Allowable Country codes
  try {
    if (denyNode["country"]) {
      YAML::Node country = denyNode["country"];
      if (!country.IsNull()) {
        if (country.IsSequence()) {
          for (auto &&i : country) {
            allow_country.insert_or_assign(i.as<std::string>(), false);
          }
        } else {
          TSDebug(PLUGIN_NAME, "Invalid country code allow list yaml");
        }
      }
    }
  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file country code deny list for maxmind", e.what());
    return false;
  }

  // Load Denyable IPs
  try {
    if (denyNode["ip"]) {
      YAML::Node ip = denyNode["ip"];
      if (!ip.IsNull()) {
        if (ip.IsSequence()) {
          // Do IP Deny processing
          for (auto &&i : ip) {
            IpAddr min, max;
            ats_ip_range_parse(std::string_view{i.as<std::string>()}, min, max);
            deny_ip_map.fill(min, max, nullptr);
            TSDebug(PLUGIN_NAME, "loading ip: valid: %d, fam %d ", min.isValid(), min.family());
          }
        } else {
          TSDebug(PLUGIN_NAME, "Invalid IP deny list yaml");
        }
      }
    }
  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file ip deny list for maxmind", e.what());
    return false;
  }

  // regex at one point was only based on path, however at one point
  // we introduced regex based on host as well "regex_host", so this supports
  // both the old "regex" for backwards compatability for configs as well as the newer
  // "regex_path"
  if (denyNode["regex"]) {
    YAML::Node regex = denyNode["regex"];
    parseregex(regex, PATH, false);
  }

  if (denyNode["regex_path"]) {
    YAML::Node regex = denyNode["regex_path"];
    parseregex(regex, PATH, false);
  }

  if (denyNode["regex_host"]) {
    YAML::Node regex = denyNode["regex_host"];
    parseregex(regex, HOST, false);
  }

#if 0
  std::unordered_map<std::string, bool>::iterator cursor;
  TSDebug(PLUGIN_NAME, "Deny Country List:");
  for (cursor = allow_country.begin(); cursor != allow_country.end(); cursor++) {
    TSDebug(PLUGIN_NAME, "%s:%d", cursor->first.c_str(), cursor->second);
  }
#endif

  return true;
}

// Parse the allow list country codes and IPs
bool
Acl::loadallow(const YAML::Node &allowNode)
{
  if (!allowNode) {
    TSDebug(PLUGIN_NAME, "No Allow rules set");
    return false;
  }
  if (allowNode.IsNull()) {
    TSDebug(PLUGIN_NAME, "Allow rules are NULL");
    return false;
  }

#if 0
  // Test junk
  for (YAML::const_iterator it = allowNode.begin(); it != allowNode.end(); ++it) {
    const std::string &name    = it->first.as<std::string>();
    YAML::NodeType::value type = it->second.Type();
    TSDebug(PLUGIN_NAME, "name: %s, value: %d", name.c_str(), type);
  }
#endif

  // Load Allowable Country codes
  try {
    if (allowNode["country"]) {
      YAML::Node country = allowNode["country"];
      if (!country.IsNull()) {
        if (country.IsSequence()) {
          for (auto &&i : country) {
            allow_country.insert_or_assign(i.as<std::string>(), true);
          }

        } else {
          TSDebug(PLUGIN_NAME, "Invalid country code allow list yaml");
        }
      }
    }
  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file country code allow list for maxmind", e.what());
    return false;
  }

  // Load Allowable IPs
  try {
    if (allowNode["ip"]) {
      YAML::Node ip = allowNode["ip"];
      if (!ip.IsNull()) {
        if (ip.IsSequence()) {
          // Do IP Allow processing
          for (auto &&i : ip) {
            IpAddr min, max;
            ats_ip_range_parse(std::string_view{i.as<std::string>()}, min, max);
            allow_ip_map.fill(min, max, nullptr);
            TSDebug(PLUGIN_NAME, "loading ip: valid: %d, fam %d ", min.isValid(), min.family());
          }
        } else {
          TSDebug(PLUGIN_NAME, "Invalid IP allow list yaml");
        }
      }
    }
  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file ip allow list for maxmind", e.what());
    return false;
  }

  // regex at one point was only based on path, however we introduced
  // regex based on host as well "regex_host", so this supports
  // both the old "regex" for backwards compatability for configs as well as the newer
  // "regex_path"
  if (allowNode["regex"]) {
    YAML::Node regex = allowNode["regex"];
    parseregex(regex, PATH, true);
  }

  if (allowNode["regex_path"]) {
    YAML::Node regex = allowNode["regex_path"];
    parseregex(regex, PATH, true);
  }

  if (allowNode["regex_host"]) {
    YAML::Node regex = allowNode["regex_host"];
    parseregex(regex, HOST, true);
  }
#if 0
  std::unordered_map<std::string, bool>::iterator cursor;
  TSDebug(PLUGIN_NAME, "Allow Country List:");
  for (cursor = allow_country.begin(); cursor != allow_country.end(); cursor++) {
    TSDebug(PLUGIN_NAME, "%s:%d", cursor->first.c_str(), cursor->second);
  }
#endif

  return true;
}

void
Acl::parseregex(const YAML::Node &regex, regex_type type, bool allow)
{
  try {
    if (!regex.IsNull()) {
      if (regex.IsSequence()) {
        // Parse each country-regex pair
        for (const auto &i : regex) {
          plugin_regex temp;
          auto temprule = i.as<std::vector<std::string>>();
          temp._regex_s = temprule.back();
          const char *error;
          int erroffset;
          temp._rex = pcre_compile(temp._regex_s.c_str(), 0, &error, &erroffset, nullptr);

          // Compile the regex for this set of countries
          if (nullptr != temp._rex) {
            temp._extra = pcre_study(temp._rex, 0, &error);
            if ((nullptr == temp._extra) && error && (*error != 0)) {
              TSError("[%s] Failed to study regular expression in %s:%s", PLUGIN_NAME, temp._regex_s.c_str(), error);
              return;
            }
          } else {
            TSError("[%s] Failed to compile regular expression in %s: %s", PLUGIN_NAME, temp._regex_s.c_str(), error);
            return;
          }

          for (std::size_t y = 0; y < temprule.size() - 1; y++) {
            TSDebug(PLUGIN_NAME, "Adding regex: %s, for country: %s", temp._regex_s.c_str(), i[y].as<std::string>().c_str());
            if (allow) {
              if (type == PATH) {
                allow_regex_path[i[y].as<std::string>()].push_back(temp);
              } else if (type == HOST) {
                allow_regex_host[i[y].as<std::string>()].push_back(temp);
              }
            } else {
              if (type == PATH) {
                deny_regex_path[i[y].as<std::string>()].push_back(temp);
              } else if (type == HOST) {
                deny_regex_host[i[y].as<std::string>()].push_back(temp);
              }
            }
          }
        }
      }
    }
  } catch (const YAML::Exception &e) {
    TSDebug(PLUGIN_NAME, "YAML::Exception %s when parsing YAML config file regex allow list for maxmind", e.what());
    return;
  }
}

void
Acl::loadhtml(const YAML::Node &htmlNode)
{
  std::string htmlname, htmlloc;
  std::ifstream f;

  if (!htmlNode) {
    TSDebug(PLUGIN_NAME, "No html field set");
    return;
  }

  if (htmlNode.IsNull()) {
    TSDebug(PLUGIN_NAME, "Html field not set");
    return;
  }

  htmlname = htmlNode.as<std::string>();
  if (htmlname[0] != '/') {
    htmlloc = TSConfigDirGet();
    htmlloc += "/";
    htmlloc.append(htmlname);
  } else {
    htmlloc.assign(htmlname);
  }

  f.open(htmlloc, std::ios::in);
  if (f.is_open()) {
    _html.append(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    f.close();
    TSDebug(PLUGIN_NAME, "Loaded HTML from %s", htmlloc.c_str());
  } else {
    TSError("[%s] Unable to open HTML file %s", PLUGIN_NAME, htmlloc.c_str());
  }
}
///////////////////////////////////////////////////////////////////////////////
// Load the maxmind database from the config parameter
bool
Acl::loaddb(const YAML::Node &dbNode)
{
  std::string dbloc, dbname;

  if (!dbNode) {
    TSDebug(PLUGIN_NAME, "No Database field set");
    return false;
  }
  if (dbNode.IsNull()) {
    TSDebug(PLUGIN_NAME, "Database file not set");
    return false;
  }
  dbname = dbNode.as<std::string>();
  if (dbname[0] != '/') {
    dbloc = TSConfigDirGet();
    dbloc += "/";
    dbloc.append(dbname);
  } else {
    dbloc.assign(dbname);
  }

  // Make sure we close any previously opened DBs in case this is a reload
  if (_db_loaded) {
    MMDB_close(&_mmdb);
  }

  int status = MMDB_open(dbloc.c_str(), MMDB_MODE_MMAP, &_mmdb);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "Can't open DB %s - %s", dbloc.c_str(), MMDB_strerror(status));
    return false;
  }
  _db_loaded = true;
  TSDebug(PLUGIN_NAME, "Initialized MMDB with %s", dbloc.c_str());
  return true;
}

bool
Acl::eval(TSRemapRequestInfo *rri, TSHttpTxn txnp)
{
  bool ret = default_allow;
  int mmdb_error;

  auto sockaddr = TSHttpTxnClientAddrGet(txnp);

  if (sockaddr == nullptr) {
    TSDebug(PLUGIN_NAME, "Err during TsHttpClientAddrGet, nullptr returned");
    ret = false;
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(&_mmdb, sockaddr, &mmdb_error);
  if (MMDB_SUCCESS != mmdb_error) {
    TSDebug(PLUGIN_NAME, "Error during sockaddr lookup: %s", MMDB_strerror(mmdb_error));
    ret = false;
    return ret;
  }

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (result.found_entry) {
    int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
    if (MMDB_SUCCESS != status) {
      TSDebug(PLUGIN_NAME, "Error looking up entry data: %s", MMDB_strerror(status));
      ret = false;
      return ret;
    }

    if (nullptr != entry_data_list) {
      // This is useful to be able to dump out a full record of a
      // mmdb entry for debug. Enabling can help if you want to figure
      // out how to add new fields
#if 0
      // Block of test stuff to dump output, remove later
      char buffer[4096];
      FILE *temp = fmemopen(&buffer[0], 4096, "wb+");
      int status = MMDB_dump_entry_data_list(temp, entry_data_list, 0);
      fflush(temp);
      TSDebug(PLUGIN_NAME, "Entry: %s, status: %s, type: %d", buffer, MMDB_strerror(status), entry_data_list->entry_data.type);
#endif

      MMDB_entry_data_s entry_data;
      int path_len = 0, host_len = 0;
      const char *path = nullptr;
      const char *host = nullptr;
      if (!allow_regex_path.empty() || !deny_regex_path.empty()) {
        path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);
      }

      if (!allow_regex_host.empty() || !deny_regex_host.empty()) {
        TSMBuffer hbuf;
        TSMLoc hloc;
        if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txnp, &hbuf, &hloc)) {
          host = TSUrlHostGet(hbuf, hloc, &host_len);
          TSHandleMLocRelease(hbuf, TS_NULL_MLOC, hloc);
        }
      }

      // Test for country code
      if (!allow_country.empty() || !allow_regex_path.empty() || !deny_regex_path.empty() || !allow_regex_host.empty() ||
          !deny_regex_host.empty()) {
        status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
        if (MMDB_SUCCESS != status) {
          TSDebug(PLUGIN_NAME, "err on get country code value: %s", MMDB_strerror(status));
          return false;
        }
        if (entry_data.has_data) {
          ret = eval_country(&entry_data, path, path_len, host, host_len);
        }
      } else {
        // Country map is empty as well as regexes, use our default rejection
        ret = default_allow;
      }

      // We have mmdb data, check if we want anonymous blocking checked
      // If blocked here, then block as well
      if (_anonymous_blocking) {
        if (!eval_anonymous(&result.entry)) {
          TSDebug(PLUGIN_NAME, "Blocking Anonymous IP");
          ret = false;
        }
      }
    }
  } else {
    TSDebug(PLUGIN_NAME, "No Country Code entry for this IP was found");
    ret = false;
  }

  // Test for allowable IPs based on our lists
  switch (eval_ip(TSHttpTxnClientAddrGet(txnp))) {
  case ALLOW_IP:
    TSDebug(PLUGIN_NAME, "Saw explicit allow of this IP");
    ret = true;
    break;
  case DENY_IP:
    TSDebug(PLUGIN_NAME, "Saw explicit deny of this IP");
    ret = false;
    break;
  case UNKNOWN_IP:
    TSDebug(PLUGIN_NAME, "Unknown IP, following default from ruleset: %d", ret);
    break;
  default:
    TSDebug(PLUGIN_NAME, "Unknown client addr ip state, should not get here");
    ret = false;
    break;
  }

  if (nullptr != entry_data_list) {
    MMDB_free_entry_data_list(entry_data_list);
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////////
// Returns true if entry data contains an
// allowable non-anonymous IP.
// False otherwise
bool
Acl::eval_anonymous(MMDB_entry_s *entry)
{
  MMDB_entry_data_s entry_data;
  int status;

  // For each value we only care if it was successful and if there was data.
  // We can have unsuccessful gets in the instance an IP is not one of the things
  // we are asking for, so its not an error
  if (_anonymous_ip) {
    status = MMDB_get_value(entry, &entry_data, "is_anonymous", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw is_anonymous set to true bool");
          return false;
        }
      }
    }
  }

  if (_anonymous_vpn) {
    status = MMDB_get_value(entry, &entry_data, "is_anonymous_vpn", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw is_anonymous vpn set to true bool");
          return false;
        }
      }
    }
  }

  if (_hosting_provider) {
    status = MMDB_get_value(entry, &entry_data, "is_hosting_provider", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw is_hosting set to true bool");
          return false;
        }
      }
    }
  }

  if (_public_proxy) {
    status = MMDB_get_value(entry, &entry_data, "is_public_proxy", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw public_proxy set to true bool");
          return false;
        }
      }
    }
  }

  if (_tor_exit_node) {
    status = MMDB_get_value(entry, &entry_data, "is_tor_exit_node", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw is_tor_exit_node set to true bool");
          return false;
        }
      }
    }
  }

  if (_residential_proxy) {
    status = MMDB_get_value(entry, &entry_data, "is_residential_proxy", NULL);
    if ((MMDB_SUCCESS == status) && (entry_data.has_data)) {
      if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
        if (entry_data.boolean == true) {
          TSDebug(PLUGIN_NAME, "saw is_residential set to true bool");
          return false;
        }
      }
    }
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Returns true if entry data contains an
// allowable country code from our map.
// False otherwise
bool
Acl::eval_country(MMDB_entry_data_s *entry_data, const char *path, int path_len, const char *host, int host_len)
{
  bool ret     = false;
  bool allow   = default_allow;
  char *output = nullptr;

  // We need to null terminate the iso_code ourselves, they are unterminated in the DBs
  output = static_cast<char *>(malloc((sizeof(char) * (entry_data->data_size + 1))));
  strncpy(output, entry_data->utf8_string, entry_data->data_size);
  output[entry_data->data_size] = '\0';
  TSDebug(PLUGIN_NAME, "This IP Country Code: %s", output);
  auto exists = allow_country.count(output);

  // If the country exists in our map then set its allow value here
  // Otherwise we will use our default value
  if (exists) {
    allow = allow_country[output];
  }

  if (allow) {
    TSDebug(PLUGIN_NAME, "Found country code of IP in allow list or allow by default");
    ret = true;
  }

  // test for path regex
  if (nullptr != path && 0 != path_len) {
    if (!allow_regex_path[output].empty()) {
      for (auto &i : allow_regex_path[output]) {
        if (PCRE_ERROR_NOMATCH != pcre_exec(i._rex, i._extra, path, path_len, 0, PCRE_NOTEMPTY, nullptr, 0)) {
          TSDebug(PLUGIN_NAME, "Got a regex allow hit on regex path: %s, country: %s", i._regex_s.c_str(), output);
          ret = true;
        }
      }
    }
    if (!deny_regex_path[output].empty()) {
      for (auto &i : deny_regex_path[output]) {
        if (PCRE_ERROR_NOMATCH != pcre_exec(i._rex, i._extra, path, path_len, 0, PCRE_NOTEMPTY, nullptr, 0)) {
          TSDebug(PLUGIN_NAME, "Got a regex deny hit on regex path: %s, country: %s", i._regex_s.c_str(), output);
          ret = false;
        }
      }
    }
  }
  // test for host regex
  if (nullptr != host && 0 != host_len) {
    if (!allow_regex_host[output].empty()) {
      for (auto &i : allow_regex_host[output]) {
        if (PCRE_ERROR_NOMATCH != pcre_exec(i._rex, i._extra, host, host_len, 0, PCRE_NOTEMPTY, nullptr, 0)) {
          TSDebug(PLUGIN_NAME, "Got a regex allow hit on regex host: %s, country: %s", i._regex_s.c_str(), output);
          ret = true;
        }
      }
    }
    if (!deny_regex_host[output].empty()) {
      for (auto &i : deny_regex_host[output]) {
        if (PCRE_ERROR_NOMATCH != pcre_exec(i._rex, i._extra, host, host_len, 0, PCRE_NOTEMPTY, nullptr, 0)) {
          TSDebug(PLUGIN_NAME, "Got a regex deny hit on regex host: %s, country: %s", i._regex_s.c_str(), output);
          ret = false;
        }
      }
    }
  }

  free(output);
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
// Returns enum based on current client:
// ALLOW_IP if IP is in the allow list
// DENY_IP if IP is in the deny list
// UNKNOWN_IP if it does not exist in either, this is then used to determine
//  action based on the default allow action
ipstate
Acl::eval_ip(const sockaddr *sock) const
{
#if 0
  for (auto &spot : allow_ip_map) {
    char text[INET6_ADDRSTRLEN];
    TSDebug(PLUGIN_NAME, "IP: %s", ats_ip_ntop(spot.min(), text, sizeof text));
    if (0 != ats_ip_addr_cmp(spot.min(), spot.max())) {
      TSDebug(PLUGIN_NAME, "stuff: %s", ats_ip_ntop(spot.max(), text, sizeof text));
    }
  }
#endif

  if (allow_ip_map.contains(sock, nullptr)) {
    // Allow map has this ip, we know we want to allow it
    return ALLOW_IP;
  }

  if (deny_ip_map.contains(sock, nullptr)) {
    // Deny map has this ip, explicitly deny
    return DENY_IP;
  }

  return UNKNOWN_IP;
}
