/** @file

    Include file for the traffic_top stats.

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
#if HAS_CURL
#include <curl/curl.h>
#endif
#include <map>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cinttypes>
#include <sys/time.h>
#include "mgmtapi.h"

using namespace std;

struct LookupItem {
  LookupItem(const char *s, const char *n, const int t) : pretty(s), name(n), numerator(""), denominator(""), type(t) {}
  LookupItem(const char *s, const char *n, const char *d, const int t) : pretty(s), name(n), numerator(n), denominator(d), type(t)
  {
  }
  const char *pretty;
  const char *name;
  const char *numerator;
  const char *denominator;
  int type;
};
extern size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream);
#if HAS_CURL
extern char curl_error[CURL_ERROR_SIZE];
#endif
extern string response;

namespace constant
{
const char global[]    = "\"global\": {\n";
const char start[]     = "\"proxy.process.";
const char separator[] = "\": \"";
const char end[]       = "\",\n";
}; // namespace constant

//----------------------------------------------------------------------------
class Stats
{
public:
  Stats(const string &url) : _url(url)
  {
    if (url != "") {
      if (_url.substr(0, 4) != "http") {
        // looks like it is a host using it the old way
        _url = "http://" + _url + "/_stats";
      }

      // set the host
      size_t start = _url.find(":");
      size_t end   = _url.find("/", start + 3);
      _host        = _url.substr(start + 3, end - start - 3);
      end          = _host.find(":");
      if (end != string::npos) {
        _host = _host.substr(0, end);
      }
    } else {
      char hostname[25];
      hostname[sizeof(hostname) - 1] = '\0';
      gethostname(hostname, sizeof(hostname) - 1);
      _host = hostname;
    }

    _time_diff = 0;
    _old_time  = 0;
    _now       = 0;
    _time      = (struct timeval){0, 0};
    _stats     = nullptr;
    _old_stats = nullptr;
    _absolute  = false;
    lookup_table.insert(make_pair("version", LookupItem("Version", "proxy.process.version.server.short", 1)));
    lookup_table.insert(make_pair("disk_used", LookupItem("Disk Used", "proxy.process.cache.bytes_used", 1)));
    lookup_table.insert(make_pair("disk_total", LookupItem("Disk Total", "proxy.process.cache.bytes_total", 1)));
    lookup_table.insert(make_pair("ram_used", LookupItem("Ram Used", "proxy.process.cache.ram_cache.bytes_used", 1)));
    lookup_table.insert(make_pair("ram_total", LookupItem("Ram Total", "proxy.process.cache.ram_cache.total_bytes", 1)));
    lookup_table.insert(make_pair("lookups", LookupItem("Lookups", "proxy.process.http.cache_lookups", 2)));
    lookup_table.insert(make_pair("cache_writes", LookupItem("Writes", "proxy.process.http.cache_writes", 2)));
    lookup_table.insert(make_pair("cache_updates", LookupItem("Updates", "proxy.process.http.cache_updates", 2)));
    lookup_table.insert(make_pair("cache_deletes", LookupItem("Deletes", "proxy.process.http.cache_deletes", 2)));
    lookup_table.insert(make_pair("read_active", LookupItem("Read Active", "proxy.process.cache.read.active", 1)));
    lookup_table.insert(make_pair("write_active", LookupItem("Writes Active", "proxy.process.cache.write.active", 1)));
    lookup_table.insert(make_pair("update_active", LookupItem("Update Active", "proxy.process.cache.update.active", 1)));
    lookup_table.insert(make_pair("entries", LookupItem("Entries", "proxy.process.cache.direntries.used", 1)));
    lookup_table.insert(make_pair("avg_size", LookupItem("Avg Size", "disk_used", "entries", 3)));

    lookup_table.insert(make_pair("dns_entry", LookupItem("DNS Entry", "proxy.process.hostdb.cache.current_items", 1)));
    lookup_table.insert(make_pair("dns_hits", LookupItem("DNS Hits", "proxy.process.hostdb.total_hits", 2)));
    lookup_table.insert(make_pair("dns_lookups", LookupItem("DNS Lookups", "proxy.process.hostdb.total_lookups", 2)));

    // Incoming HTTP/1.1 and HTTP/2 connections - some metrics are HTTP version specific
    lookup_table.insert(make_pair("client_req", LookupItem("Requests", "proxy.process.http.incoming_requests", 2)));

    // total_client_connections
    lookup_table.insert(
      make_pair("client_conn_h1", LookupItem("New Conn HTTP/1.x", "proxy.process.http.total_client_connections", 2)));
    lookup_table.insert(
      make_pair("client_conn_h2", LookupItem("New Conn HTTP/2", "proxy.process.http2.total_client_connections", 2)));
    lookup_table.insert(make_pair("client_conn", LookupItem("New Conn", "client_conn_h1", "client_conn_h2", 6)));

    // requests / connections
    lookup_table.insert(make_pair("client_req_conn", LookupItem("Req/Conn", "client_req", "client_conn", 3)));

    // current_client_connections
    lookup_table.insert(
      make_pair("client_curr_conn_h1", LookupItem("Curr Conn HTTP/1.x", "proxy.process.http.current_client_connections", 1)));
    lookup_table.insert(
      make_pair("client_curr_conn_h2", LookupItem("Curr Conn HTTP/2", "proxy.process.http2.current_client_connections", 1)));
    lookup_table.insert(make_pair("client_curr_conn", LookupItem("Curr Conn", "client_curr_conn_h1", "client_curr_conn_h2", 6)));

    // current_active_client_connections
    lookup_table.insert(make_pair("client_actv_conn_h1",
                                  LookupItem("Active Con HTTP/1.x", "proxy.process.http.current_active_client_connections", 1)));
    lookup_table.insert(make_pair("client_actv_conn_h2",
                                  LookupItem("Active Con HTTP/2", "proxy.process.http2.current_active_client_connections", 1)));
    lookup_table.insert(make_pair("client_actv_conn", LookupItem("Active Con", "client_actv_conn_h1", "client_actv_conn_h2", 6)));

    lookup_table.insert(make_pair("server_req", LookupItem("Requests", "proxy.process.http.outgoing_requests", 2)));
    lookup_table.insert(make_pair("server_conn", LookupItem("New Conn", "proxy.process.http.total_server_connections", 2)));
    lookup_table.insert(make_pair("server_req_conn", LookupItem("Req/Conn", "server_req", "server_conn", 3)));
    lookup_table.insert(make_pair("server_curr_conn", LookupItem("Curr Conn", "proxy.process.http.current_server_connections", 1)));

    lookup_table.insert(
      make_pair("client_head", LookupItem("Head Bytes", "proxy.process.http.user_agent_response_header_total_size", 2)));
    lookup_table.insert(
      make_pair("client_body", LookupItem("Body Bytes", "proxy.process.http.user_agent_response_document_total_size", 2)));
    lookup_table.insert(
      make_pair("server_head", LookupItem("Head Bytes", "proxy.process.http.origin_server_response_header_total_size", 2)));
    lookup_table.insert(
      make_pair("server_body", LookupItem("Body Bytes", "proxy.process.http.origin_server_response_document_total_size", 2)));

    // not used directly
    lookup_table.insert(make_pair("ram_hit", LookupItem("Ram Hit", "proxy.process.cache.ram_cache.hits", 2)));
    lookup_table.insert(make_pair("ram_miss", LookupItem("Ram Misses", "proxy.process.cache.ram_cache.misses", 2)));
    lookup_table.insert(make_pair("ka_total", LookupItem("KA Total", "proxy.process.net.dynamic_keep_alive_timeout_in_total", 2)));
    lookup_table.insert(make_pair("ka_count", LookupItem("KA Count", "proxy.process.net.dynamic_keep_alive_timeout_in_count", 2)));

    lookup_table.insert(make_pair("client_abort", LookupItem("Clnt Abort", "proxy.process.http.err_client_abort_count_stat", 2)));
    lookup_table.insert(make_pair("conn_fail", LookupItem("Conn Fail", "proxy.process.http.err_connect_fail_count_stat", 2)));
    lookup_table.insert(make_pair("abort", LookupItem("Abort", "proxy.process.http.transaction_counts.errors.aborts", 2)));
    lookup_table.insert(
      make_pair("t_conn_fail", LookupItem("Conn Fail", "proxy.process.http.transaction_counts.errors.connect_failed", 2)));
    lookup_table.insert(make_pair("other_err", LookupItem("Other Err", "proxy.process.http.transaction_counts.errors.other", 2)));

    // percentage
    lookup_table.insert(make_pair("ram_ratio", LookupItem("Ram Hit", "ram_hit", "ram_hit_miss", 4)));
    lookup_table.insert(make_pair("dns_ratio", LookupItem("DNS Hit", "dns_hits", "dns_lookups", 4)));

    // percentage of requests
    lookup_table.insert(make_pair("fresh", LookupItem("Fresh", "proxy.process.http.transaction_counts.hit_fresh", 5)));
    lookup_table.insert(make_pair("reval", LookupItem("Revalidate", "proxy.process.http.transaction_counts.hit_revalidated", 5)));
    lookup_table.insert(make_pair("cold", LookupItem("Cold", "proxy.process.http.transaction_counts.miss_cold", 5)));
    lookup_table.insert(make_pair("changed", LookupItem("Changed", "proxy.process.http.transaction_counts.miss_changed", 5)));
    lookup_table.insert(make_pair("not", LookupItem("Not Cache", "proxy.process.http.transaction_counts.miss_not_cacheable", 5)));
    lookup_table.insert(make_pair("no", LookupItem("No Cache", "proxy.process.http.transaction_counts.miss_client_no_cache", 5)));

    lookup_table.insert(
      make_pair("fresh_time", LookupItem("Fresh (ms)", "proxy.process.http.transaction_totaltime.hit_fresh", "fresh", 8)));
    lookup_table.insert(
      make_pair("reval_time", LookupItem("Reval (ms)", "proxy.process.http.transaction_totaltime.hit_revalidated", "reval", 8)));
    lookup_table.insert(
      make_pair("cold_time", LookupItem("Cold (ms)", "proxy.process.http.transaction_totaltime.miss_cold", "cold", 8)));
    lookup_table.insert(
      make_pair("changed_time", LookupItem("Chang (ms)", "proxy.process.http.transaction_totaltime.miss_changed", "changed", 8)));
    lookup_table.insert(
      make_pair("not_time", LookupItem("Not (ms)", "proxy.process.http.transaction_totaltime.miss_not_cacheable", "not", 8)));
    lookup_table.insert(
      make_pair("no_time", LookupItem("No (ms)", "proxy.process.http.transaction_totaltime.miss_client_no_cache", "no", 8)));

    lookup_table.insert(make_pair("get", LookupItem("GET", "proxy.process.http.get_requests", 5)));
    lookup_table.insert(make_pair("head", LookupItem("HEAD", "proxy.process.http.head_requests", 5)));
    lookup_table.insert(make_pair("post", LookupItem("POST", "proxy.process.http.post_requests", 5)));

    lookup_table.insert(make_pair("100", LookupItem("100", "proxy.process.http.100_responses", 5)));
    lookup_table.insert(make_pair("101", LookupItem("101", "proxy.process.http.101_responses", 5)));
    lookup_table.insert(make_pair("1xx", LookupItem("1xx", "proxy.process.http.1xx_responses", 5)));
    lookup_table.insert(make_pair("200", LookupItem("200", "proxy.process.http.200_responses", 5)));
    lookup_table.insert(make_pair("201", LookupItem("201", "proxy.process.http.201_responses", 5)));
    lookup_table.insert(make_pair("202", LookupItem("202", "proxy.process.http.202_responses", 5)));
    lookup_table.insert(make_pair("203", LookupItem("203", "proxy.process.http.203_responses", 5)));
    lookup_table.insert(make_pair("204", LookupItem("204", "proxy.process.http.204_responses", 5)));
    lookup_table.insert(make_pair("205", LookupItem("205", "proxy.process.http.205_responses", 5)));
    lookup_table.insert(make_pair("206", LookupItem("206", "proxy.process.http.206_responses", 5)));
    lookup_table.insert(make_pair("2xx", LookupItem("2xx", "proxy.process.http.2xx_responses", 5)));
    lookup_table.insert(make_pair("300", LookupItem("300", "proxy.process.http.300_responses", 5)));
    lookup_table.insert(make_pair("301", LookupItem("301", "proxy.process.http.301_responses", 5)));
    lookup_table.insert(make_pair("302", LookupItem("302", "proxy.process.http.302_responses", 5)));
    lookup_table.insert(make_pair("303", LookupItem("303", "proxy.process.http.303_responses", 5)));
    lookup_table.insert(make_pair("304", LookupItem("304", "proxy.process.http.304_responses", 5)));
    lookup_table.insert(make_pair("305", LookupItem("305", "proxy.process.http.305_responses", 5)));
    lookup_table.insert(make_pair("307", LookupItem("307", "proxy.process.http.307_responses", 5)));
    lookup_table.insert(make_pair("3xx", LookupItem("3xx", "proxy.process.http.3xx_responses", 5)));
    lookup_table.insert(make_pair("400", LookupItem("400", "proxy.process.http.400_responses", 5)));
    lookup_table.insert(make_pair("401", LookupItem("401", "proxy.process.http.401_responses", 5)));
    lookup_table.insert(make_pair("402", LookupItem("402", "proxy.process.http.402_responses", 5)));
    lookup_table.insert(make_pair("403", LookupItem("403", "proxy.process.http.403_responses", 5)));
    lookup_table.insert(make_pair("404", LookupItem("404", "proxy.process.http.404_responses", 5)));
    lookup_table.insert(make_pair("405", LookupItem("405", "proxy.process.http.405_responses", 5)));
    lookup_table.insert(make_pair("406", LookupItem("406", "proxy.process.http.406_responses", 5)));
    lookup_table.insert(make_pair("407", LookupItem("407", "proxy.process.http.407_responses", 5)));
    lookup_table.insert(make_pair("408", LookupItem("408", "proxy.process.http.408_responses", 5)));
    lookup_table.insert(make_pair("409", LookupItem("409", "proxy.process.http.409_responses", 5)));
    lookup_table.insert(make_pair("410", LookupItem("410", "proxy.process.http.410_responses", 5)));
    lookup_table.insert(make_pair("411", LookupItem("411", "proxy.process.http.411_responses", 5)));
    lookup_table.insert(make_pair("412", LookupItem("412", "proxy.process.http.412_responses", 5)));
    lookup_table.insert(make_pair("413", LookupItem("413", "proxy.process.http.413_responses", 5)));
    lookup_table.insert(make_pair("414", LookupItem("414", "proxy.process.http.414_responses", 5)));
    lookup_table.insert(make_pair("415", LookupItem("415", "proxy.process.http.415_responses", 5)));
    lookup_table.insert(make_pair("416", LookupItem("416", "proxy.process.http.416_responses", 5)));
    lookup_table.insert(make_pair("4xx", LookupItem("4xx", "proxy.process.http.4xx_responses", 5)));
    lookup_table.insert(make_pair("500", LookupItem("500", "proxy.process.http.500_responses", 5)));
    lookup_table.insert(make_pair("501", LookupItem("501", "proxy.process.http.501_responses", 5)));
    lookup_table.insert(make_pair("502", LookupItem("502", "proxy.process.http.502_responses", 5)));
    lookup_table.insert(make_pair("503", LookupItem("503", "proxy.process.http.503_responses", 5)));
    lookup_table.insert(make_pair("504", LookupItem("504", "proxy.process.http.504_responses", 5)));
    lookup_table.insert(make_pair("505", LookupItem("505", "proxy.process.http.505_responses", 5)));
    lookup_table.insert(make_pair("5xx", LookupItem("5xx", "proxy.process.http.5xx_responses", 5)));

    lookup_table.insert(make_pair("s_100", LookupItem("100 B", "proxy.process.http.response_document_size_100", 5)));
    lookup_table.insert(make_pair("s_1k", LookupItem("1 KB", "proxy.process.http.response_document_size_1K", 5)));
    lookup_table.insert(make_pair("s_3k", LookupItem("3 KB", "proxy.process.http.response_document_size_3K", 5)));
    lookup_table.insert(make_pair("s_5k", LookupItem("5 KB", "proxy.process.http.response_document_size_5K", 5)));
    lookup_table.insert(make_pair("s_10k", LookupItem("10 KB", "proxy.process.http.response_document_size_10K", 5)));
    lookup_table.insert(make_pair("s_1m", LookupItem("1 MB", "proxy.process.http.response_document_size_1M", 5)));
    lookup_table.insert(make_pair("s_>1m", LookupItem("> 1 MB", "proxy.process.http.response_document_size_inf", 5)));

    // sum together
    lookup_table.insert(make_pair("ram_hit_miss", LookupItem("Ram Hit+Miss", "ram_hit", "ram_miss", 6)));
    lookup_table.insert(make_pair("client_net", LookupItem("Net (bits)", "client_head", "client_body", 7)));
    lookup_table.insert(make_pair("client_size", LookupItem("Total Size", "client_head", "client_body", 6)));
    lookup_table.insert(make_pair("client_avg_size", LookupItem("Avg Size", "client_size", "client_req", 3)));

    lookup_table.insert(make_pair("server_net", LookupItem("Net (bits)", "server_head", "server_body", 7)));
    lookup_table.insert(make_pair("server_size", LookupItem("Total Size", "server_head", "server_body", 6)));
    lookup_table.insert(make_pair("server_avg_size", LookupItem("Avg Size", "server_size", "server_req", 3)));

    lookup_table.insert(make_pair("total_time", LookupItem("Total Time", "proxy.process.http.total_transactions_time", 2)));

    // ratio
    lookup_table.insert(make_pair("client_req_time", LookupItem("Resp (ms)", "total_time", "client_req", 3)));
    lookup_table.insert(make_pair("client_dyn_ka", LookupItem("Dynamic KA", "ka_total", "ka_count", 3)));
  }

  void
  getStats()
  {
    if (_url == "") {
      int64_t value = 0;
      if (_old_stats != nullptr) {
        delete _old_stats;
        _old_stats = nullptr;
      }
      _old_stats = _stats;
      _stats     = new map<string, string>;

      gettimeofday(&_time, nullptr);
      double now = _time.tv_sec + (double)_time.tv_usec / 1000000;

      for (map<string, LookupItem>::const_iterator lookup_it = lookup_table.begin(); lookup_it != lookup_table.end(); ++lookup_it) {
        const LookupItem &item = lookup_it->second;

        if (item.type == 1 || item.type == 2 || item.type == 5 || item.type == 8) {
          if (strcmp(item.pretty, "Version") == 0) {
            // special case for Version information
            TSString strValue = nullptr;
            if (TSRecordGetString(item.name, &strValue) == TS_ERR_OKAY) {
              string key     = item.name;
              (*_stats)[key] = strValue;
              TSfree(strValue);
            } else {
              fprintf(stderr, "Error getting stat: %s when calling TSRecordGetString() failed: file \"%s\", line %d\n\n", item.name,
                      __FILE__, __LINE__);
              abort();
            }
          } else {
            if (TSRecordGetInt(item.name, &value) != TS_ERR_OKAY) {
              fprintf(stderr, "Error getting stat: %s when calling TSRecordGetInt() failed: file \"%s\", line %d\n\n", item.name,
                      __FILE__, __LINE__);
              abort();
            }
            string key = item.name;
            char buffer[32];
            sprintf(buffer, "%" PRId64, value);
            string foo     = buffer;
            (*_stats)[key] = foo;
          }
        }
      }
      _old_time  = _now;
      _now       = now;
      _time_diff = _now - _old_time;
    } else {
#if HAS_CURL
      CURL *curl;
      CURLcode res;

      curl = curl_easy_init();
      if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, _url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

        // update time
        gettimeofday(&_time, nullptr);
        double now = _time.tv_sec + (double)_time.tv_usec / 1000000;

        response.clear();
        response.reserve(32768); // should hopefully be smaller then 32KB
        res = curl_easy_perform(curl);

        // only if success update stats and time information
        if (res == 0) {
          if (_old_stats != nullptr) {
            delete _old_stats;
            _old_stats = nullptr;
          }
          _old_stats = _stats;
          _stats     = new map<string, string>;

          // parse
          parseResponse(response);
          _old_time  = _now;
          _now       = now;
          _time_diff = _now - _old_time;
        } else {
          fprintf(stderr, "Can't fetch url %s\n", _url.c_str());
          exit(1);
        }

        /* always cleanup */
        curl_easy_cleanup(curl);
      }
#endif
    }
  }

  int64_t
  getValue(const string &key, const map<string, string> *stats) const
  {
    map<string, string>::const_iterator stats_it = stats->find(key);
    if (stats_it == stats->end()) {
      return 0;
    }
    int64_t value = atoll(stats_it->second.c_str());
    return value;
  }

  void
  getStat(const string &key, double &value, int overrideType = 0)
  {
    string strtmp;
    int typetmp;
    getStat(key, value, strtmp, typetmp, overrideType);
  }

  void
  getStat(const string &key, string &value)
  {
    map<string, LookupItem>::const_iterator lookup_it = lookup_table.find(key);
    assert(lookup_it != lookup_table.end());
    const LookupItem &item = lookup_it->second;

    map<string, string>::const_iterator stats_it = _stats->find(item.name);
    if (stats_it == _stats->end()) {
      value = "";
    } else {
      value = stats_it->second.c_str();
    }
  }

  void
  getStat(const string &key, double &value, string &prettyName, int &type, int overrideType = 0)
  {
    map<string, LookupItem>::const_iterator lookup_it = lookup_table.find(key);
    assert(lookup_it != lookup_table.end());
    const LookupItem &item = lookup_it->second;
    prettyName             = item.pretty;
    if (overrideType != 0) {
      type = overrideType;
    } else {
      type = item.type;
    }

    if (type == 1 || type == 2 || type == 5 || type == 8) {
      value = getValue(item.name, _stats);
      if (key == "total_time") {
        value = value / 10000000;
      }

      if ((type == 2 || type == 5 || type == 8) && _old_stats != nullptr && _absolute == false) {
        double old = getValue(item.name, _old_stats);
        if (key == "total_time") {
          old = old / 10000000;
        }
        value = (value - old) / _time_diff;
      }
    } else if (type == 3 || type == 4) {
      double numerator   = 0;
      double denominator = 0;
      getStat(item.numerator, numerator);
      getStat(item.denominator, denominator);
      if (denominator == 0) {
        value = 0;
      } else {
        value = numerator / denominator;
      }
      if (type == 4) {
        value *= 100;
      }
    } else if (type == 6 || type == 7) {
      double numerator;
      double denominator;
      getStat(item.numerator, numerator, 2);
      getStat(item.denominator, denominator, 2);
      value = numerator + denominator;
      if (type == 7) {
        value *= 8;
      }
    }

    if (type == 8) {
      double denominator;
      getStat(item.denominator, denominator, 2);
      if (denominator == 0) {
        value = 0;
      } else {
        value = value / denominator * 1000;
      }
    }

    if (type == 5) {
      double denominator = 0;
      getStat("client_req", denominator);
      if (denominator == 0) {
        value = 0;
      } else {
        value = value / denominator * 100;
      }
    }
  }

  bool
  toggleAbsolute()
  {
    if (_absolute == true) {
      _absolute = false;
    } else {
      _absolute = true;
    }

    return _absolute;
  }

  void
  parseResponse(const string &response)
  {
    // move past global
    size_t pos = response.find(constant::global);
    pos += sizeof(constant::global) - 1;

    // find parts of the line
    while (true) {
      size_t start     = response.find(constant::start, pos);
      size_t separator = response.find(constant::separator, pos);
      size_t end       = response.find(constant::end, pos);

      if (start == string::npos || separator == string::npos || end == string::npos) {
        return;
      }

      // cout << constant::start << " " << start << endl;
      // cout << constant::separator << " " << separator << endl;
      // cout << constant::end << " " << end << endl;

      string key = response.substr(start + 1, separator - start - 1);
      string value =
        response.substr(separator + sizeof(constant::separator) - 1, end - separator - sizeof(constant::separator) + 1);

      (*_stats)[key] = value;
      // cout << "key " << key << " " << "value " << value << endl;
      pos = end + sizeof(constant::end) - 1;
      // cout << "pos: " << pos << endl;
    }
  }

  const string &
  getHost() const
  {
    return _host;
  }

  ~Stats()
  {
    if (_stats != nullptr) {
      delete _stats;
    }
    if (_old_stats != nullptr) {
      delete _old_stats;
    }
  }

private:
  map<string, string> *_stats;
  map<string, string> *_old_stats;
  map<string, LookupItem> lookup_table;
  string _url;
  string _host;
  double _old_time;
  double _now;
  double _time_diff;
  struct timeval _time;
  bool _absolute;
};
