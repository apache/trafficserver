/** @file

  This is the primary include file for the proxy cache system.

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
#include "raft.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "raft.pb.h"
#include "raft_impl.h"
#include "gtest/gtest.h"

using ::std::deque;
using ::std::map;
using ::std::set;
using ::std::string;
using ::std::unique_ptr;
using ::std::vector;
using ::std::pair;
using ::std::to_string;

const int kMaxServers = 10;

namespace raft
{
class RaftTest;

class RaftServer : public RaftServerInterface
{
public:
  typedef RaftMessagePb Message;
  typedef RaftLogEntryPb LogEntry;
  typedef RaftConfigPb Config;
  typedef Raft<RaftServer> RaftClass;

  RaftServer(const string node, RaftTest *test)
  {
    node_ = node;
    test_ = test;
    raft_.reset(NewRaft(this, node));
  }

  bool SendMessage(RaftClass *raft, const string &node, const Message &message);

  void
  GetLogEntry(RaftClass *raft, int64_t term, int64_t start, int64_t end, LogEntry *entry)
  {
    if (use_commit_log_) {
      for (auto &e : commits_) {
        if (e->term() < term)
          continue;
        if (e->index() >= start) {
          entry->CopyFrom(*e);
          return;
        }
      }
    } else {
      for (auto &e : log_) {
        if (e->term() < term)
          continue;
        if (e->has_index() && e->index() >= start) {
          entry->CopyFrom(*e);
          return;
        }
      }
    }
    entry->Clear();
  }

  void
  WriteLogEntry(RaftClass *raft, const LogEntry &entry)
  {
    log_.emplace_back(new LogEntry(entry));
  }

  void
  CommitLogEntry(RaftClass *raft, const LogEntry &entry)
  {
    commits_.emplace_back(new LogEntry(entry));
    string s = entry.data();
    auto p   = s.find("=");
    if (p != string::npos)
      state_[s.substr(0, p)] = make_pair(entry.index(), s.substr(p + 1));
  }

  void
  LeaderChange(RaftClass *raft, const string &leader)
  {
    leader_ = leader;
  }

  void
  ConfigChange(RaftClass *raft, const Config &config)
  {
    config_.reset(new Config(config));
  }

  bool use_commit_log_ = false;
  RaftTest *test_;
  unique_ptr<Config> config_;
  string node_;
  string leader_;
  unique_ptr<RaftClass> raft_;
  vector<unique_ptr<LogEntry>> log_;
  vector<unique_ptr<LogEntry>> commits_;
  map<string, pair<int64_t, string>> state_;
};

template <typename T>
bool
firstless(const T &a, const T &b)
{
  return a.first < b.first;
}

class RaftTest : public ::testing::Test
{
public:
  typedef RaftServer::Message Message;
  typedef RaftServer::LogEntry LogEntry;
  typedef RaftServer::Config Config;

  void
  SendMessage(const string &from, const string &to, const Message &message)
  {
    if (down_.count(from) || down_.count(to))
      return;
    messages_.emplace_back(make_pair(to, new Message(message)));
  }

  void
  ForwardMessages()
  {
    while (!messages_.empty()) {
      auto &p = messages_.front();
      for (auto &s : servers_)
        if (p.first == s->node_)
          s->raft_->Run(now_, *p.second);
      delete p.second;
      messages_.pop_front();
    }
  }

protected:
  RaftTest() : now_(0) {}
  virtual ~RaftTest()
  {
    for (auto &p : messages_)
      delete p.second;
  }

  // 20 ticks gives 2 full election timeouts because the timeouts are random
  // on any given node and very from [1, 2) timeouts.
  void
  Ticks(int n)
  {
    for (int i = 0; i < n; i++) {
      now_ += 0.1;
      for (auto &s : servers_) {
        s->raft_->Tick(now_);
        ForwardMessages();
      }
    }
  }

  void
  StartUp(int n, const LogEntry &config_log_entry)
  {
    int offset = servers_.size();
    for (int i = offset; i < n + offset; i++) {
      servers_.emplace_back(new RaftServer(to_string(i), this));
      auto &raft = *servers_[i]->raft_.get();
      raft.Recover(config_log_entry);
      raft.Start(0, i);
    }
  }

  void
  CrashAndRecover(int i, const LogEntry &config_log_entry)
  {
    vector<unique_ptr<LogEntry>> log;
    for (auto &p : servers_[i]->log_)
      log.emplace_back(p.release());
    servers_[i].reset(new RaftServer(to_string(i), this));
    auto &raft = *servers_[i]->raft_.get();
    raft.Recover(config_log_entry);
    for (auto &p : log) {
      raft.Recover(*p);
      servers_[i]->log_.emplace_back(p.release());
    }
    raft.Start(now_, i);
  }

  void
  CrashAndBurn(int i, const LogEntry &config_log_entry)
  {
    servers_[i].reset(new RaftServer(to_string(i), this));
    auto &raft = *servers_[i]->raft_.get();
    raft.Recover(config_log_entry);
    raft.Start(now_, i);
  }

  void
  SnapshotCrashAndRecover(int i, const LogEntry &config_log_entry)
  {
    vector<LogEntry> entries;
    vector<pair<int64_t, string>> state;
    for (auto &p : servers_[i]->state_)
      state.push_back(make_pair(p.second.first, p.first + "=" + p.second.second));
    std::sort(state.begin(), state.end(), firstless<pair<int64_t, string>>);
    servers_[i]->log_.clear();
    for (auto &s : state) {
      LogEntry *e = new LogEntry;
      e->set_index(s.first);
      e->set_data(s.second);
      servers_[i]->log_.emplace_back(e);
    }
    auto &raft = *servers_[i]->raft_.get();
    raft.Snapshot(false, &entries);
    for (auto &e : entries)
      servers_[i]->log_.emplace_back(new LogEntry(e));
    servers_[i]->state_.clear();
    CrashAndRecover(i, config_log_entry);
  }

  LogEntry
  ConfigLogEntry(int n)
  {
    LogEntry config_log_entry;
    for (int i = 0; i < n; i++)
      config_log_entry.mutable_config()->add_node(to_string(i));
    return config_log_entry;
  }

  double now_;
  set<string> down_;
  vector<unique_ptr<RaftServer>> servers_;
  deque<pair<string, Message *>> messages_;
};

bool
RaftServer::SendMessage(RaftClass *raft, const string &node, const Message &message)
{
  test_->SendMessage(node_, node, message);
  return true;
}

TEST_F(RaftTest, OneEmptyConfig)
{
  servers_.emplace_back(new RaftServer("0", this));
  auto &raft = *servers_[0]->raft_.get();
  raft.Start(0, 0);
  Ticks(20);
  EXPECT_EQ(servers_[0]->leader_, "0");
}

TEST_F(RaftTest, One)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  StartUp(1, config_log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[0]->leader_, "0");
}

TEST_F(RaftTest, OneTwoNotParticipating)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  // Startup server 0 as leader.
  StartUp(1, config_log_entry);
  Ticks(20);
  // Startup server 1 with config with 0 as leader.
  StartUp(1, config_log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[0]->leader_, "0");
  EXPECT_EQ(servers_[1]->leader_, "0");
}

TEST_F(RaftTest, OneTwo)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  // Startup server 0 as leader.
  StartUp(1, config_log_entry);
  Ticks(20);
  // Startup server 1 with config with 0 as leader.
  StartUp(1, config_log_entry);
  Ticks(20);
  // Add 1 into consensus.
  {
    auto &raft = *servers_[0]->raft_.get();
    LogEntry config_log_entry;
    config_log_entry.mutable_config()->add_node("0");
    config_log_entry.mutable_config()->add_node("1");
    raft.Propose(config_log_entry);
    Ticks(20);
  }
  EXPECT_EQ(servers_[0]->leader_, "0");
  EXPECT_EQ(servers_[1]->leader_, "0");
  EXPECT_EQ(servers_[0]->commits_.size(), 1);
  EXPECT_EQ(servers_[1]->commits_.size(), 1);
}

TEST_F(RaftTest, OneTwoSwitchToTwo)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  // Startup servers 0, and 1 with 0 as leader.
  StartUp(1, config_log_entry);
  StartUp(1, config_log_entry);
  Ticks(20);
  // Add 1 into consensus.
  {
    auto &raft = *servers_[0]->raft_.get();
    LogEntry config_log_entry(ConfigLogEntry(2));
    raft.Propose(config_log_entry);
    Ticks(20);
  }
  EXPECT_EQ(servers_[0]->leader_, "0");
  EXPECT_EQ(servers_[1]->leader_, "0");
  // Switch to only having 1.
  {
    auto &raft = *servers_[0]->raft_.get();
    LogEntry config_log_entry;
    config_log_entry.mutable_config()->add_node("1");
    config_log_entry.mutable_config()->add_replica("0");
    raft.Propose(config_log_entry);
    Ticks(20);
  }
  EXPECT_EQ(servers_[0]->leader_, "1");
  EXPECT_EQ(servers_[1]->leader_, "1");
}

TEST_F(RaftTest, OneThenTwo)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  // Startup servers 0 and 1, with 0 as leader.
  StartUp(1, config_log_entry);
  StartUp(1, config_log_entry);
  Ticks(20);
  // Switch to only having 1.
  {
    auto &raft = *servers_[0]->raft_.get();
    LogEntry config_log_entry;
    config_log_entry.mutable_config()->add_node("1");
    raft.Propose(config_log_entry);
    Ticks(20);
  }
  EXPECT_EQ(servers_[0]->leader_, "1");
  EXPECT_EQ(servers_[1]->leader_, "1");
}

TEST_F(RaftTest, OneAndTwo)
{
  LogEntry config_log_entry(ConfigLogEntry(2));
  // Startup servers 0, and 1 in nodes.
  StartUp(2, config_log_entry);
  Ticks(20);
  EXPECT_NE(servers_[0]->leader_, "");
  EXPECT_EQ(servers_[1]->leader_, servers_[0]->leader_);
}

TEST_F(RaftTest, OneAndTwoAndThree)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1 and 2 in nodes.
  StartUp(3, config_log_entry);
  Ticks(20);
  EXPECT_NE(servers_[0]->leader_, "");
  EXPECT_EQ(servers_[1]->leader_, servers_[0]->leader_);
  EXPECT_EQ(servers_[2]->leader_, servers_[0]->leader_);
}

TEST_F(RaftTest, OneAndTwoNotThree)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1 with config [0, 1, 2].
  StartUp(2, config_log_entry);
  Ticks(20);
  EXPECT_NE(servers_[0]->leader_, "");
  EXPECT_EQ(servers_[1]->leader_, servers_[0]->leader_);
}

TEST_F(RaftTest, OneAndTwoThenTwoAndThree)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1 with config [0, 1, 2].
  StartUp(2, config_log_entry);
  Ticks(20);
  // Startup server 2 with config [0, 1, 2] and down 0.
  StartUp(1, config_log_entry);
  down_.insert("0");
  Ticks(20);
  EXPECT_EQ(servers_[0]->leader_, "");
  EXPECT_NE(servers_[1]->leader_, "");
  EXPECT_NE(servers_[1]->leader_, "0");
  EXPECT_EQ(servers_[2]->leader_, servers_[1]->leader_);
}

TEST_F(RaftTest, OneTwoThreeThenAbdicate)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1, 2 with config [0, 1, 2].
  StartUp(3, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  raft.Stop();
  down_.insert(to_string(ileader));
  Ticks(1); // Abdication will cause immediate reelection.
  EXPECT_NE(servers_[(ileader + 1) % 3]->leader_, "");
  EXPECT_EQ(servers_[(ileader + 1) % 3]->leader_, servers_[(ileader + 2) % 3]->leader_);
}

TEST_F(RaftTest, OneTwoThreeThenAllSeparate)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1, 2 with config [0, 1, 2].
  StartUp(3, config_log_entry);
  Ticks(20);
  down_.insert("0");
  down_.insert("1");
  down_.insert("2");
  Ticks(20);
  EXPECT_EQ(servers_[0]->leader_, "");
  EXPECT_EQ(servers_[1]->leader_, "");
  EXPECT_EQ(servers_[2]->leader_, "");
}

TEST_F(RaftTest, OneTwoThreeThenAllSeparateThenTogether)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  // Startup servers 0, 1, 2 with config [0, 1, 2].
  StartUp(3, config_log_entry);
  Ticks(20);
  down_.insert("0");
  down_.insert("1");
  down_.insert("2");
  Ticks(20);
  down_.clear();
  Ticks(20);
  EXPECT_NE(servers_[0]->leader_, "");
  EXPECT_EQ(servers_[1]->leader_, servers_[0]->leader_);
  EXPECT_EQ(servers_[2]->leader_, servers_[0]->leader_);
}

TEST_F(RaftTest, OneLog)
{
  LogEntry config_log_entry;
  StartUp(1, config_log_entry);
  auto &raft = *servers_[0]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  EXPECT_EQ(servers_[0]->log_.size(), 1);
  EXPECT_EQ(servers_[0]->log_[0]->data(), "a");
  EXPECT_EQ(servers_[0]->commits_.size(), 1);
  EXPECT_EQ(servers_[0]->commits_[0]->data(), "a");
}

TEST_F(RaftTest, OneLogLog)
{
  LogEntry config_log_entry;
  StartUp(1, config_log_entry);
  auto &raft = *servers_[0]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  EXPECT_EQ(servers_[0]->log_.size(), 2);
  EXPECT_EQ(servers_[0]->log_[0]->data(), "a");
  EXPECT_EQ(servers_[0]->log_[1]->data(), "b");
  EXPECT_EQ(servers_[0]->commits_.size(), 2);
  EXPECT_EQ(servers_[0]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[0]->commits_[1]->data(), "b");
}

TEST_F(RaftTest, OneTwoLogLog)
{
  LogEntry config_log_entry(ConfigLogEntry(2));
  StartUp(2, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  int iother  = ileader ? 0 : 1;
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  Ticks(20);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[ileader]->log_.size(), 7);
  EXPECT_NE(servers_[ileader]->log_[0]->vote(), "");   // vote.
  EXPECT_NE(servers_[ileader]->log_[1]->leader(), ""); // election.
  EXPECT_EQ(servers_[ileader]->log_[2]->data_committed(), servers_[ileader]->log_[1]->index());
  EXPECT_EQ(servers_[ileader]->log_[3]->data(), "a");
  EXPECT_EQ(servers_[ileader]->log_[4]->data_committed(), servers_[ileader]->log_[3]->index());
  EXPECT_EQ(servers_[ileader]->log_[5]->data(), "b");
  EXPECT_EQ(servers_[ileader]->log_[6]->data_committed(), servers_[ileader]->log_[5]->index());
  EXPECT_EQ(servers_[ileader]->commits_.size(), 2);
  EXPECT_EQ(servers_[ileader]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[ileader]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[iother]->commits_.size(), 2);
  EXPECT_EQ(servers_[iother]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[iother]->commits_[1]->data(), "b");
}

TEST_F(RaftTest, OneTwoThreeLogDownLogUp)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  StartUp(3, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  Ticks(20);
  int downer = (ileader + 1) % 3;
  down_.insert(to_string(downer));
  Ticks(20);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[downer]->commits_.size(), 1);
  EXPECT_EQ(servers_[downer]->commits_[0]->data(), "a");
  down_.clear();
  Ticks(20);
  Ticks(20);
  for (auto i : {0, 1, 2}) {
    EXPECT_EQ(servers_[i]->commits_.size(), 2);
    EXPECT_EQ(servers_[i]->commits_[0]->data(), "a");
    EXPECT_EQ(servers_[i]->commits_[1]->data(), "b");
  }
}

TEST_F(RaftTest, OneTwoThreeLogLogThreeDamagedLogRestore)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  StartUp(3, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  Ticks(20);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  int downer = (ileader + 1) % 3;
  // Lose the "a" commit.
  servers_[downer]->log_.erase(servers_[downer]->log_.begin() + 3);
  CrashAndRecover(downer, config_log_entry);
  Ticks(20);
  for (auto i : {0, 1, 2}) {
    EXPECT_EQ(servers_[i]->commits_.size(), 2);
    EXPECT_EQ(servers_[i]->commits_[0]->data(), "a");
    EXPECT_EQ(servers_[i]->commits_[1]->data(), "b");
  }
}

TEST_F(RaftTest, OneTwoLogLogThenThree)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  config_log_entry.mutable_config()->add_node("1");
  StartUp(2, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  StartUp(1, config_log_entry); // Start node 2.
  config_log_entry.mutable_config()->add_node("2");
  raft.Propose(config_log_entry); // Change config to [0, 1, 2].
  Ticks(20);
  EXPECT_EQ(servers_[1]->commits_.size(), 3);
  EXPECT_EQ(servers_[1]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[1]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[1]->commits_[2]->config().node_size(), 3);
  // Verify that the log is replicated.
  EXPECT_EQ(servers_[2]->commits_.size(), 3);
  EXPECT_EQ(servers_[2]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[2]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[2]->commits_[2]->config().node_size(), 3);
}

TEST_F(RaftTest, OneRecover)
{
  LogEntry config_log_entry;
  StartUp(1, config_log_entry);
  {
    auto &raft = *servers_[0]->raft_.get();
    LogEntry log_entry;
    log_entry.set_data("a");
    raft.Propose(log_entry);
  }
  Ticks(20);
  CrashAndRecover(0, config_log_entry);
  EXPECT_EQ(servers_[0]->commits_.size(), 1);
  EXPECT_EQ(servers_[0]->commits_[0]->data(), "a");
}

TEST_F(RaftTest, OneTwoThreeCrashAndBurnLeader)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  config_log_entry.mutable_config()->add_node("1");
  config_log_entry.mutable_config()->add_node("2");
  StartUp(3, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[ileader]->commits_.size(), 2);
  EXPECT_EQ(servers_[ileader]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[ileader]->commits_[1]->data(), "b");
  CrashAndBurn(ileader, config_log_entry);
  Ticks(20);
  // Verify that the log is replicated.
  for (auto i : {0, 1, 2}) {
    EXPECT_EQ(servers_[i]->commits_.size(), 2);
    EXPECT_EQ(servers_[i]->commits_[0]->data(), "a");
    EXPECT_EQ(servers_[i]->commits_[1]->data(), "b");
  }
}

TEST_F(RaftTest, FiveCrashLeaderAndAnotherAndRecover)
{
  LogEntry config_log_entry(ConfigLogEntry(5));
  StartUp(5, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[ileader]->commits_.size(), 2);
  EXPECT_EQ(servers_[ileader]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[ileader]->commits_[1]->data(), "b");
  CrashAndRecover(ileader, config_log_entry);
  CrashAndRecover((ileader + 1) % 5, config_log_entry);
  Ticks(20);
  // Verify that the log is replicated.
  EXPECT_EQ(servers_[ileader]->commits_.size(), 2);
  EXPECT_EQ(servers_[ileader]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[ileader]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_.size(), 2);
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_[1]->data(), "b");
}

TEST_F(RaftTest, FiveCrashAndBurnLeaderAndAnother)
{
  LogEntry config_log_entry(ConfigLogEntry(5));
  StartUp(5, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  CrashAndBurn(ileader, config_log_entry);
  CrashAndBurn((ileader + 1) % 5, config_log_entry);
  Ticks(20);
  // Verify that the log is replicated.
  EXPECT_EQ(servers_[ileader]->commits_.size(), 2);
  EXPECT_EQ(servers_[ileader]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[ileader]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_.size(), 2);
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[(ileader + 1) % 5]->commits_[1]->data(), "b");
}

// Test that a log from a leader without quorum never is committed and that a
// log with the same index from a leader with quorum is.
TEST_F(RaftTest, FiveLogDown3LogDown2Up3LogUp2)
{
  LogEntry config_log_entry(ConfigLogEntry(5));
  StartUp(5, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  down_.insert(to_string((ileader + 1) % 5));
  down_.insert(to_string((ileader + 2) % 5));
  down_.insert(to_string((ileader + 3) % 5));
  auto &raft = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  down_.clear();
  down_.insert(to_string((ileader + 4) % 5));
  down_.insert(to_string(ileader));
  Ticks(20);
  int ileader2 = servers_[((ileader + 1) % 5)]->leader_[0] - '0';
  auto &raft2  = *servers_[ileader2]->raft_.get();
  log_entry.set_data("c");
  raft2.Propose(log_entry);
  log_entry.set_data("d");
  raft2.Propose(log_entry);
  Ticks(20);
  down_.clear();
  Ticks(20);
  Ticks(20);
  for (auto i : {0, 1, 2, 3, 4}) {
    EXPECT_EQ(servers_[i]->commits_.size(), 2);
    EXPECT_EQ(servers_[i]->commits_[0]->data(), "c");
    EXPECT_EQ(servers_[i]->commits_[1]->data(), "d");
  }
}

TEST_F(RaftTest, ReplicaFailover)
{
  LogEntry config_log_entry;
  config_log_entry.mutable_config()->add_node("0");
  config_log_entry.mutable_config()->add_replica("1");
  StartUp(2, config_log_entry);
  Ticks(20);
  auto &raft = *servers_[0]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a");
  raft.Propose(log_entry);
  log_entry.set_data("b");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[0]->commits_.size(), 2);
  EXPECT_EQ(servers_[0]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[0]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[1]->commits_.size(), 2);
  EXPECT_EQ(servers_[1]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[1]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[0]->leader_, "0");
  EXPECT_EQ(servers_[1]->leader_, "0");
  config_log_entry.mutable_config()->clear_node();
  config_log_entry.mutable_config()->clear_replica();
  config_log_entry.mutable_config()->add_node("1");
  config_log_entry.mutable_config()->add_replica("0");
  CrashAndBurn(0, config_log_entry);
  CrashAndRecover(1, config_log_entry);
  Ticks(20);
  // Verify that the log is replicated.
  EXPECT_EQ(servers_[0]->commits_.size(), 2);
  EXPECT_EQ(servers_[0]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[0]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[1]->commits_.size(), 2);
  EXPECT_EQ(servers_[1]->commits_[0]->data(), "a");
  EXPECT_EQ(servers_[1]->commits_[1]->data(), "b");
  EXPECT_EQ(servers_[0]->leader_, "1");
  EXPECT_EQ(servers_[1]->leader_, "1");
}

TEST_F(RaftTest, OneSnapshotTwo)
{
  LogEntry config_log_entry;
  StartUp(1, config_log_entry);
  auto &raft = *servers_[0]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a=1");
  raft.Propose(log_entry);
  log_entry.set_data("b=2");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[0]->state_["a"].second, "1");
  EXPECT_EQ(servers_[0]->state_["b"].second, "2");
  log_entry.set_data("b=3");
  raft.Propose(log_entry);
  Ticks(20);
  EXPECT_EQ(servers_[0]->state_["a"].second, "1");
  EXPECT_EQ(servers_[0]->state_["b"].second, "3");
  SnapshotCrashAndRecover(0, config_log_entry);
  Ticks(20);
  // Verify that the state is restored.
  EXPECT_EQ(servers_[0]->state_["a"].second, "1");
  EXPECT_EQ(servers_[0]->state_["b"].second, "3");
}

TEST_F(RaftTest, OneTwoThreeSnapshotOneTwoCrashAndBurnThree)
{
  LogEntry config_log_entry(ConfigLogEntry(3));
  StartUp(3, config_log_entry);
  Ticks(20);
  int ileader = servers_[0]->leader_[0] - '0';
  auto &raft  = *servers_[ileader]->raft_.get();
  LogEntry log_entry;
  log_entry.set_data("a=1");
  raft.Propose(log_entry);
  log_entry.set_data("b=2");
  raft.Propose(log_entry);
  Ticks(20);
  log_entry.set_data("b=3");
  raft.Propose(log_entry);
  Ticks(20);
  SnapshotCrashAndRecover(0, config_log_entry);
  SnapshotCrashAndRecover(1, config_log_entry);
  CrashAndBurn(2, config_log_entry);
  Ticks(20);
  // Verify that the state is restored.
  EXPECT_EQ(servers_[0]->state_["a"].second, "1");
  EXPECT_EQ(servers_[0]->state_["b"].second, "3");
  EXPECT_EQ(servers_[2]->state_["a"].second, "1");
  EXPECT_EQ(servers_[2]->state_["b"].second, "3");
}
} // namespace raft
