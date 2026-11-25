/** @file

    Unit tests for plugin.yaml parsing

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

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "proxy/Plugin.h"

namespace
{
class TempYAML
{
public:
  explicit TempYAML(const std::string &content)
  {
    // Unique per instance: test cases run as separate, concurrent ctest processes,
    // so a fixed name would let one case delete another's file mid-read.
    static std::atomic<unsigned> seq{0};

    _path = std::filesystem::temp_directory_path() /
            ("test_plugin_yaml." + std::to_string(getpid()) + "." + std::to_string(seq++) + ".yaml");
    std::ofstream f(_path);
    f << content;
  }

  ~TempYAML()
  {
    std::error_code ec; // Best effort: cleanup must not throw out of a destructor.
    std::filesystem::remove(_path, ec);
  }

  const char *
  path() const
  {
    return _path.c_str();
  }

private:
  std::filesystem::path _path;
};

/// A unique temporary path that is removed, whatever it became, on scope exit.
class TempPath
{
public:
  explicit TempPath(const std::string &tag)
  {
    _path = std::filesystem::temp_directory_path() / ("test_plugin_yaml." + std::to_string(getpid()) + "." + tag + ".yaml");
  }

  ~TempPath()
  {
    std::error_code ec;
    std::filesystem::remove_all(_path, ec);
  }

  const char *
  path() const
  {
    return _path.c_str();
  }

private:
  std::filesystem::path _path;
};
} // namespace

TEST_CASE("parse_plugin_yaml - minimal valid config", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: stats_over_http.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].enabled == true);
  CHECK(result.value[0].load_order == -1);
  CHECK(result.value[0].params.empty());
  CHECK(result.value[0].config_literal.empty());
}

TEST_CASE("parse_plugin_yaml - all fields populated", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: abuse.so
    enabled: true
    load_order: 100
    params:
      - etc/trafficserver/abuse.config
      - --verbose
      - --debug
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "abuse.so");
  CHECK(result.value[0].enabled == true);
  CHECK(result.value[0].load_order == 100);
  REQUIRE(result.value[0].params.size() == 3);
  CHECK(result.value[0].params[0] == "etc/trafficserver/abuse.config");
  CHECK(result.value[0].params[1] == "--verbose");
  CHECK(result.value[0].params[2] == "--debug");
}

TEST_CASE("parse_plugin_yaml - enabled false", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: debug_plugin.so
    enabled: false
  - path: stats_over_http.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 2);
  CHECK(result.value[0].enabled == false);
  CHECK(result.value[1].enabled == true);
}

TEST_CASE("parse_plugin_yaml - load_order sorting", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: third.so
    load_order: 300
  - path: first.so
    load_order: 100
  - path: second.so
    load_order: 200
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "first.so");
  CHECK(result.value[1].path == "second.so");
  CHECK(result.value[2].path == "third.so");
}

TEST_CASE("parse_plugin_yaml - ordered before unordered", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: unordered_first.so
  - path: unordered_second.so
  - path: ordered.so
    load_order: 50
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "ordered.so");
  CHECK(result.value[1].path == "unordered_first.so");
  CHECK(result.value[2].path == "unordered_second.so");
}

TEST_CASE("parse_plugin_yaml - stable sort preserves sequence order on ties", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: b.so
    load_order: 100
  - path: a.so
    load_order: 100
  - path: c.so
    load_order: 100
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);
  CHECK(result.value[0].path == "b.so");
  CHECK(result.value[1].path == "a.so");
  CHECK(result.value[2].path == "c.so");
}

TEST_CASE("parse_plugin_yaml - inline config literal (scalar)", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: header_rewrite.so
    config: |
      cond %{SEND_RESPONSE_HDR_HOOK}
         set-header X-Debug "true"
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "header_rewrite.so");
  CHECK(result.value[0].config_literal.find("set-header X-Debug") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - inline config rejects structured YAML mapping", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: txn_box.so
    config:
      when: proxy-req
      do:
        - set-header:
            name: X-Forwarded-For
            value: inbound-addr-remote
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
}

TEST_CASE("parse_plugin_yaml - inline config rejects structured YAML sequence", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: custom.so
    config:
      - rule1
      - rule2
      - rule3
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
}

TEST_CASE("parse_plugin_yaml - missing path field", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - enabled: true
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("missing required 'path' field") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - malformed entry is a config error", "[plugin_yaml]")
{
  // Each case has a good first entry, so the error must name entry #2.
  auto content = GENERATE(as<std::string>{},
                          // A bare scalar instead of a mapping.
                          "plugins:\n  - path: good.so\n  - libfoo.so\n",
                          // Values of the wrong type.
                          "plugins:\n  - path: good.so\n  - path: libfoo.so\n    enabled: maybe\n",
                          "plugins:\n  - path: good.so\n  - path: libfoo.so\n    load_order: x\n",
                          "plugins:\n  - path: good.so\n  - path: [libfoo.so]\n",
                          "plugins:\n  - path: good.so\n  - path: libfoo.so\n    params:\n      - {a: b}\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("plugin entry #2:") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - path with no value is missing", "[plugin_yaml]")
{
  // yaml-cpp converts a null to the string "null", which would become the path.
  auto content = GENERATE(as<std::string>{}, "plugins:\n  - path: good.so\n  - path:\n",
                          "plugins:\n  - path: good.so\n  - path: ~\n    enabled: true\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("plugin entry #2 missing required 'path' field") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - params that is not a list of values", "[plugin_yaml]")
{
  // Each case has a good first entry, so the error must name entry #2.
  auto [content, expected] = GENERATE(table<std::string, std::string>({
    // A scalar or a mapping instead of a list.
    {"plugins: [{path: good.so}, {path: libfoo.so, params: --verbose}]\n",       "plugin entry #2: 'params' must be a list"  },
    {"plugins: [{path: good.so}, {path: libfoo.so, params: {verbose: true}}]\n", "plugin entry #2: 'params' must be a list"  },
    // A list item with no value.
    {"plugins: [{path: good.so}, {path: libfoo.so, params: [--verbose, ~]}]\n",  "plugin entry #2: 'params' item #2 is empty"},
  }));

  CAPTURE(content, expected);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()) == expected);
}

TEST_CASE("parse_plugin_yaml - params with no value passes no arguments", "[plugin_yaml]")
{
  // A params key whose items are all commented out is the same as an empty list.
  TempYAML yaml("plugins:\n  - path: libfoo.so\n    params:\n#      - --verbose\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].params.empty());
}

TEST_CASE("parse_plugin_yaml - negative load_order is a config error", "[plugin_yaml]")
{
  // A negative value reads as unset, so the plugin would load after every
  // ordered plugin instead of before them.
  TempYAML yaml("plugins:\n  - path: good.so\n  - path: first.so\n    load_order: -1\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("plugin entry #2: 'load_order' must not be negative") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - load_order zero is accepted", "[plugin_yaml]")
{
  // Zero is the lowest valid value, so it loads before an unordered plugin.
  TempYAML yaml("plugins:\n  - path: unordered.so\n  - path: first.so\n    load_order: 0\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 2);
  CHECK(result.value[0].path == "first.so");
  CHECK(result.value[0].load_order == 0);
  CHECK(result.value[1].path == "unordered.so");
}

TEST_CASE("parse_plugin_yaml - missing plugins key", "[plugin_yaml]")
{
  TempYAML yaml(R"(
something_else:
  - path: foo.so
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("missing or invalid 'plugins' sequence") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - document is not a map", "[plugin_yaml]")
{
  // plugin.config lines pasted into plugin.yaml parse as one plain scalar, and
  // subscripting a scalar throws. A top-level sequence is not a map either.
  auto content = GENERATE(as<std::string>{}, "header_rewrite.so rules.conf\nfoo.so\n", "- path: foo.so\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("missing or invalid 'plugins' sequence") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - invalid YAML syntax", "[plugin_yaml]")
{
  TempYAML yaml("plugins:\n  - path: foo.so\n  bad indent here\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("failed to parse") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - empty first document followed by another is a config error", "[plugin_yaml]")
{
  // YAML::Load() returns only the first document, so a null first document
  // would load none of the plugins the second one names.
  auto content = GENERATE(as<std::string>{}, "---\n---\nplugins:\n  - path: foo.so\n", "~\n---\nplugins:\n  - path: foo.so\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()) == "YAML document #1 is empty and another document follows");
}

TEST_CASE("parse_plugin_yaml - nonempty second document is a config error", "[plugin_yaml]")
{
  // The plugins a second document names would be dropped.
  auto content = GENERATE(as<std::string>{}, "plugins:\n  - path: foo.so\n---\nplugins:\n  - path: bar.so\n",
                          "plugins:\n  - path: foo.so\n---\n---\nplugins:\n  - path: bar.so\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("is not empty, expected one document") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - empty documents after the first are accepted", "[plugin_yaml]")
{
  // A trailing "---" or "..." holds nothing that could be dropped.
  auto content = GENERATE(as<std::string>{}, "plugins:\n  - path: foo.so\n---\n", "plugins:\n  - path: foo.so\n---\n---\n",
                          "plugins:\n  - path: foo.so\n...\n", "plugins:\n  - path: foo.so\n...\n---\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "foo.so");
}

TEST_CASE("parse_plugin_yaml - malformed second document is a config error", "[plugin_yaml]")
{
  TempYAML yaml("plugins:\n  - path: foo.so\n---\nplugins:\n  - path: [unclosed\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("failed to parse") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - one document with start and end markers", "[plugin_yaml]")
{
  TempYAML yaml("---\nplugins:\n  - path: foo.so\n...\n");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].path == "foo.so");
}

TEST_CASE("parse_plugin_yaml - empty plugins list", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins: []
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  CHECK(result.value.empty());
}

TEST_CASE("parse_plugin_yaml - a file that names no plugins", "[plugin_yaml]")
{
  // An empty file, a comment-only file, and a plugins key with every entry
  // commented out all load no plugins, the same as plugins: [].
  auto content = GENERATE(as<std::string>{}, "", "# no plugins yet\n", "plugins:\n#  - path: foo.so\n");

  CAPTURE(content);
  TempYAML yaml(content);

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  CHECK(result.value.empty());
}

TEST_CASE("parse_plugin_yaml - multiple plugins mixed features", "[plugin_yaml]")
{
  TempYAML yaml(R"(
plugins:
  - path: stats_over_http.so

  - path: abuse.so
    params:
      - etc/trafficserver/abuse.config

  - path: header_rewrite.so
    params:
      - etc/trafficserver/header_rewrite.config

  - path: experimental.so
    enabled: false
    params:
      - --verbose
)");

  auto result = parse_plugin_yaml(yaml.path());

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 4);

  CHECK(result.value[0].path == "stats_over_http.so");
  CHECK(result.value[0].params.empty());

  CHECK(result.value[1].path == "abuse.so");
  REQUIRE(result.value[1].params.size() == 1);
  CHECK(result.value[1].params[0] == "etc/trafficserver/abuse.config");

  CHECK(result.value[2].path == "header_rewrite.so");
  REQUIRE(result.value[2].params.size() == 1);
  CHECK(result.value[2].params[0] == "etc/trafficserver/header_rewrite.config");

  CHECK(result.value[3].path == "experimental.so");
  CHECK(result.value[3].enabled == false);
}

TEST_CASE("parse_plugin_yaml - directory is a config error", "[plugin_yaml]")
{
  // A directory passes the access() check at startup. The file type check
  // rejects it before any read so the error names the cause.
  TempPath dir("dir");
  REQUIRE(std::filesystem::create_directory(dir.path()));

  auto result = parse_plugin_yaml(dir.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("not a regular file") != std::string::npos);
}

TEST_CASE("parse_plugin_yaml - FIFO is a config error", "[plugin_yaml]")
{
  // A blocking open of a FIFO for reading waits until a writer opens it, so
  // startup would hang unless the file is opened without blocking and rejected.
  TempPath fifo("fifo");
  REQUIRE(mkfifo(fifo.path(), 0600) == 0);

  // Should the check regress, opening the write end unblocks the reader, so
  // the test fails on its checks instead of hanging until the ctest timeout.
  // A non-blocking open for writing succeeds only once a reader is waiting.
  std::atomic<bool> done{false};
  std::thread       watchdog([&fifo, &done] {
    for (int i = 0; i < 20 && !done; ++i) {
      if (int fd = open(fifo.path(), O_WRONLY | O_NONBLOCK); fd >= 0) {
        close(fd);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  struct Join {
    std::thread       &thread;
    std::atomic<bool> &done;
    ~Join()
    {
      done = true;
      thread.join();
    }
  } join{watchdog, done};

  auto result = parse_plugin_yaml(fifo.path());

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("not a regular file") != std::string::npos);
}

#if defined(__linux__)
TEST_CASE("parse_plugin_yaml - read error is a config error", "[plugin_yaml]")
{
  // /proc/self/mem is a regular file whose offset 0 is unmapped, so the first
  // read() fails with EIO. The message must carry that error rather than treat
  // the failure as end of file.
  auto result = parse_plugin_yaml("/proc/self/mem");

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()) == std::string("failed to read: ") + strerror(EIO));
}
#endif

TEST_CASE("parse_plugin_yaml - nonexistent file", "[plugin_yaml]")
{
  auto result = parse_plugin_yaml("/tmp/nonexistent_plugin_yaml_test.yaml");

  REQUIRE_FALSE(result.ok());
  CHECK(std::string(result.errata.front().text()).find("failed to open: No such file or directory") != std::string::npos);
}
