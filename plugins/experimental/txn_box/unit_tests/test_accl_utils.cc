/** @file
 *  Utility helpers for string accelerator class.
 *
 * Copyright 2020, Verizon Media .
 * SPDX-License-Identifier: Apache-2.0
 */

#include "catch.hpp"
#include <iostream>
#include <forward_list>
#include <chrono>

#include <swoc/TextView.h>
#include "txn_box/accl_util.h"

TEST_CASE("Basic single char insert/full_match std::string_view")
{
  {
    StringTree<std::string_view, std::string_view> trie;

    std::forward_list<std::pair<std::string, std::string>> kv = {
      {"A", "1"},
      {"S", "2"},
      {"E", "3"},
      {"R", "4"},
      {"C", "5"},
      {"H", "6"}
    };

    for (auto const &[k, v] : kv) {
      REQUIRE(trie.insert(k, v));
    }
    // try again.
    for (auto const &[k, v] : kv) {
      REQUIRE(!trie.insert(k, v));
    }

    for (auto const &[k, v] : kv) {
      auto [found, value] = trie.full_match(k);
      REQUIRE(found);
      REQUIRE(value == v);
    }

    std::string k{"I"};
    std::string v{"7"};
    REQUIRE(trie.insert(k, v));
    auto const &[found, value] = trie.full_match(k);
    REQUIRE(found);
    REQUIRE(value == v);
  }
}

TEST_CASE("Basic insert/full_match TextView", "")
{
  {
    string_tree_map trie;

    std::forward_list<std::pair<std::string_view, std::string_view>> kv = {
      {"A", "1"},
      {"S", "2"},
      {"E", "3"},
      {"R", "4"},
      {"C", "5"},
      {"H", "6"}
    };
    for (auto const &[k, v] : kv) {
      REQUIRE(trie.insert(k, v));
    }
    // try again.
    for (auto const &[k, v] : kv) {
      REQUIRE(!trie.insert(k, v));
    }

    for (auto const &[k, v] : kv) {
      auto [found, value] = trie.full_match(k);
      REQUIRE(found);
      REQUIRE(value == v);
    }

    std::string k{"I"};
    std::string v{"7"};
    REQUIRE(trie.insert(k, v));
    auto [found, value] = trie.full_match(k);
    REQUIRE(found);
    REQUIRE(value == v);
  }
}

template <typename T>
static auto
generateKVFrom(T const &str)
{
  std::vector<std::pair<T, T>> kvs;
  T gradstr;
  for (auto const &c : str) {
    gradstr += c;
    kvs.push_back({gradstr, gradstr});
  }

  return kvs;
}

TEST_CASE("Basic Prefix match Test on std::string", "[insert][prefix_match][std::string]")
{
  StringTree<std::string, std::string> trie;
  std::vector<std::pair<std::string, std::string>> kvs = generateKVFrom(std::string{"http://www.apache.com/trafficserver"});
  for (auto const &[k, v] : kvs) {
    trie.insert(k, v);
  }

  // basic check
  for (auto const &[k, v] : kvs) {
    auto [found, value] = trie.full_match(k);
    REQUIRE(found);
    REQUIRE(value == v);
  }
  for (auto iter = std::begin(kvs); iter != std::end(kvs); ++iter) {
    auto const &keys = trie.prefix_match(iter->first);
    REQUIRE(std::equal(iter, std::end(kvs), std::begin(keys), std::end(keys)));
  }
}

TEST_CASE("Basic Prefix Match Test on a mix case strings", "")
{
  string_tree_map trie;

  trie.insert("www.yahoo.com", "www.yahoo.com/ok");
  trie.insert("www.yaHoo.com", "www.yaHoo.com/ok");
  trie.insert("www.yahoo.com/2", "www.yahoo.com/2");
  trie.insert("www.yaHoo.com/2", "www.yaHoo.com/2");
  trie.insert("www.yaHoO.com", "www.yaHoO.com/ok");
  trie.insert("www.yahoo.coM", "www.yahoo.coM/ok");
  trie.insert("www.google.com", "www.goog.le");
  trie.insert("360.yahoo.com.mx", "360.yahoo.com.mx");

  std::unordered_map<std::string, std::vector<std::string>> exp_results{
    {"www.yahoo.com",    {"www.yahoo.com", "www.yahoo.com/2"}                 },
    {"www.yaHoo.com",    {"www.yaHoo.com", "www.yaHoo.com/2"}                 },
    {"www.yahoo.com/2",  {"www.yahoo.com/2"}                                  },
    {"www.yaHoo.com/2",  {"www.yaHoo.com/2"}                                  },
    {"www.yaHoO.com",    {"www.yaHoO.com"}                                    },
    {"www.yahoo.coM",    {"www.yahoo.coM"}                                    },
    {"www.google.com",   {"www.google.com"}                                   },
    {"www.go",           {"www.google.com"}                                   },
    {"www.yah",          {"www.yahoo.com", "www.yahoo.com/2", "www.yahoo.coM"}},
    {"www.yaH",          {"www.yaHoo.com", "www.yaHoo.com/2", "www.yaHoO.com"}},
    {"360.yahoo.com.mx", {"360.yahoo.com.mx"}                                 }
  };

  for (auto const &[key, expected] : exp_results) {
    auto const &items = trie.prefix_match(key);
    INFO("Looking for " << key << ", to be found " << expected.size() << "? found " << items.size());

    REQUIRE(items.size() == expected.size());
    for (auto const &pair : items) {
      REQUIRE(std::find(std::begin(expected), std::end(expected), pair.first) != std::end(expected));
    }
  }
}

TEST_CASE("Basic Suffix Match Test", "")
{
  static const std::unordered_map<std::string, std::string> kv{
    {"Yahoo.com",                "yahoo.com"           },
    {"Yahoo.com/search/en",      "en.search.yahoo.com" },
    {"Yahoo.com/finance/Es",     "es.finance.yahoo.com"},
    {"Yahoo.com/search/es",      "es.yahoo.com"        },
    {"Yahoo.com/es",             "es.yahoo.com"        },
    {"apache.com",               "es.google.com"       },
    {"trafficserver.apache.com", "es.apache.com"       }
  };
  string_tree_map trie;

  for (auto const &[k, v] : kv) {
    REQUIRE(trie.insert(k, v));
  }

  std::unordered_map<std::string, std::vector<std::string>> exp_results{
    {"/es",                {"Yahoo.com/es", "Yahoo.com/search/es"}                        },
    {"s",                  {"Yahoo.com/es", "Yahoo.com/search/es", "Yahoo.com/finance/Es"}},
    {".com",               {"trafficserver.apache.com", "apache.com", "Yahoo.com"}        },
    {"/Es",                {"Yahoo.com/finance/Es"}                                       },
    {"/en",                {"Yahoo.com/search/en"}                                        },
    {"ahoo.com/search/en", {"Yahoo.com/search/en"}                                        }
  };

  for (auto const &[key, expected] : exp_results) {
    auto const &items = trie.suffix_match(key);
    REQUIRE(items.size() == expected.size());
    for (auto const &pair : items) {
      REQUIRE(std::find(std::begin(expected), std::end(expected), pair.first) != std::end(expected));
    }
  }
}

namespace test_helper
{
template <typename Time = std::chrono::nanoseconds, typename Clock = std::chrono::high_resolution_clock> struct func_timer {
  using unit = Time;
  template <typename F, typename... Args>
  static auto
  run(F &&f, Args &&...args)
  {
    const auto start = Clock::now();
    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    return std::chrono::duration_cast<Time>(Clock::now() - start).count();
  }
};
template <typename T> struct to_string {
  static constexpr auto value{""};
};
template <> struct to_string<std::chrono::microseconds> {
  static constexpr auto value{" microseconds"};
};
template <> struct to_string<std::chrono::nanoseconds> {
  static constexpr auto value{" nanoseconds"};
};
template <> struct to_string<std::chrono::milliseconds> {
  static constexpr auto value{" milliseconds"};
};
} // namespace test_helper

TEST_CASE("Very basic perf test")
{
  using namespace test_helper;
  static const std::unordered_map<std::string, std::string> kv{
    {"Yahoo.com",                "yahoo.com"           },
    {"Yahoo.com/search/en",      "en.search.yahoo.com" },
    {"Yahoo.com/finance/Es",     "es.finance.yahoo.com"},
    {"Yahoo.com/search/es",      "es.yahoo.com"        },
    {"Yahoo.com/es",             "es.yahoo.com"        },
    {"apache.com",               "es.apache.com"       },
    {"asf.com",                  "asf.com"             },
    {"ASF.com",                  "ASF.com"             },
    {"txn_box",                  "ok.txn_box"          },
    {"trafficserver.apache.com", "es.apache.com"       }
  };

  // We should make this more accurate and run it several time to sample the avg time
  // on inserts and find. As a first approach is ok.
  SECTION("string_tree_map")
  {
    string_tree_map trie;
    {
      func_timer<> f;
      auto const &took = f.run([&trie]() {
        for (auto const &[k, v] : kv) {
          trie.insert(k, v);
        }
      });
      std::cout << "string_tree_map - insert(twice each element for prefix/suffix) " << kv.size() << " elements took " << took
                << to_string<func_timer<>::unit>::value << std::endl;
    }
    {
      func_timer<> f;
      auto const &took = f.run([&trie]() { trie.insert("trafficserver.apache.com", "ats.apache.com"); });
      std::cout << "string_tree_map - insert single element into an existing trie took " << took
                << to_string<func_timer<>::unit>::value << std::endl;
    }
    {
      func_timer<> f;
      auto const &took = f.run([&trie]() { trie.full_match("ASF.com"); });
      std::cout << "string_tree_map - full_match(\"ASF.com\") took " << took << to_string<func_timer<>::unit>::value << std::endl;
    }
    {
      func_timer<> f;
      std::size_t found{0};
      string_tree_map::search_type search;
      auto const &took = f.run([&trie, &found]() {
        auto const &r = trie.prefix_match("Yahoo.com");
        found         = r.size();
      });
      CHECK(found == 5);
      std::cout << "string_tree_map - prefix_match(\"Yahoo.com\") took " << took << to_string<func_timer<>::unit>::value
                << ". Found " << found << " elements." << std::endl;
    }
    {
      func_timer<> f;
      std::size_t found{0};
      string_tree_map::search_type search;
      auto const &took = f.run([&trie, &found]() {
        auto const &r = trie.suffix_match("/es");
        found         = r.size();
      });
      CHECK(found == 2);
      std::cout << "string_tree_map - suffix_match(\"/es\") took " << took << to_string<func_timer<>::unit>::value << ". Found "
                << found << " elements." << std::endl;
    }
  }

  SECTION("std::unordered_map")
  {
    std::unordered_map<std::string_view, std::string_view> map;
    {
      func_timer<> f;
      auto const &took = f.run([&map]() {
        for (auto const &[k, v] : kv) {
          map.insert({k, v});
        }
      });
      std::cout << "std::unordered_map - insert " << kv.size() << " elements took " << took << to_string<func_timer<>::unit>::value
                << std::endl;
    }
    {
      func_timer<> f;
      auto const &took = f.run([&map]() { map.insert({"trafficserver.apache.com", "ats.apache.com"}); });
      std::cout << "std::unordered_map - insert single element into an existing trie took " << took
                << to_string<func_timer<>::unit>::value << std::endl;
    }
    {
      func_timer<> f;
      auto const &took = f.run([&map]() { map.find("ASF.com"); });
      std::cout << "std::unordered_map - insert(\"ASF.com\") took " << took << to_string<func_timer<>::unit>::value << std::endl;
    }
  }
}
