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
#ifndef CONSENSUS_IMPL_H_
#define CONSENSUS_IMPL_H_
#include <stdlib.h>
#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "raft.h"

namespace raft
{
template <typename Server> class RaftImpl : public Raft<Server>
{
public:
  typedef typename Server::Message Message;
  typedef typename Server::LogEntry LogEntry;
  typedef typename Server::Config Config;

  RaftImpl(Server *server, const ::std::string &node) : server_(server), node_(node) {}
  ~RaftImpl() {}
  virtual void
  SetElectionTimeout(double timeout)
  {
    election_timeout_ = timeout;
  }

  virtual void
  Recover(const LogEntry &e)
  {
    if (!e.has_term()) {   // LogEntry from server.
      if (e.has_index()) { // Summary log entry.
        ProcessLogEntry(e, true);
        Commit(true);
      } else if (e.has_config()) {
        config_.CopyFrom(e.config());
        ConfigChanged();
      }
    } else { // LogEntry has passed through Raft.
      if (e.term() > term_)
        NewTerm(e.term(), e.leader(), true);
      if (e.has_config_committed())
        config_committed_ = e.config_committed();
      if (e.has_data_committed())
        data_committed_ = e.data_committed();
      ProcessLogEntry(e, true);
      Commit(true);
    }
  }

  virtual void
  Start(double now, int64_t seed)
  {
    last_heartbeat_ = now;
    srand48_r(seed, &rand_);
    double r = 0.0;
    drand48_r(&rand_, &r);
    random_election_delay_ = election_timeout_ * r;
    if (ConfigChanged())
      NewTerm(term_ + 1, leader_, true);
    else
      vote_ = node_; // Conservatively assume we called a vote for ourself.
    server_->ConfigChange(this, config_);
    server_->LeaderChange(this, leader_);
  }

  virtual void
  Tick(double now)
  {
    if (i_am_in_nodes() && !other_nodes_.empty() && now - last_heartbeat_ > election_timeout_ + random_election_delay_) {
      double r = 0.0;
      drand48_r(&rand_, &r);
      random_election_delay_ = election_timeout_ * r;
      last_heartbeat_        = now;
      VoteForMe();
      return;
    }
    // Send heartbeats at 1/4 of timeout to allow for lost
    // packets/connections.
    if (i_am_leader() && now - last_heartbeat_sent_ > election_timeout_ / 4) {
      last_heartbeat_sent_ = now;
      ReplicateAll(true);
    }
  }

  virtual void
  Propose(const LogEntry &e)
  {
    assert(i_am_leader());
    LogEntry entry(e);
    entry.set_term(term_);
    entry.set_index(index_ + 1);
    entry.set_previous_log_term(last_log_term_);
    entry.set_previous_log_index(index_);
    ProcessLogEntry(entry, false);
    ReplicateAll(false);
    Commit(false);
  }

  virtual void
  Run(double now, const Message &m)
  {
    if (m.term() >= term_)
      seen_term_ = true;
    if (m.term() < term_)
      return; // Ignore messages from terms gone by.
    if (m.term() > term_)
      NewTerm(m.term(), m.leader(), false);
    if (m.leader() != "" && leader_ != m.leader() && other_nodes_.count(m.from())) { // Only from nodes I acknowledge.
      leader_ = m.leader();
      server_->LeaderChange(this, leader_);
    }
    auto &n = node_state_[m.from()];
    if (n.term != m.term()) {
      n.term = m.term();
      n.vote = "";
    }
    n.term           = term_;
    n.last_log_term  = m.last_log_term();
    n.last_log_index = m.last_log_index();
    if (m.from() != leader_ || m.has_vote()) {
      HandleAck(now, m, &n);
      if (m.has_vote())
        HandleVote(m, &n);
      return;
    }
    last_heartbeat_ = now;
    if (m.config_committed() > config_committed_ || m.data_committed() > data_committed_) {
      config_committed_ = m.config_committed();
      data_committed_   = m.data_committed();
      WriteInternalLogEntry();
    }
    if (m.has_entry())
      Ack(ProcessLogEntry(m.entry(), false));
    else
      Ack(m.last_log_index() == index_ && m.last_log_term() == last_log_term_);
    Commit(false);
  }

  virtual void
  Snapshot(bool uncommitted, ::std::vector<LogEntry> *entries)
  {
    entries->clear();
    LogEntry config_e;
    config_e.set_term(config_.term());
    config_e.set_index(config_.index());
    config_e.set_vote(vote_);
    config_e.set_data_committed(data_committed_);
    config_e.set_config_committed(config_committed_);
    config_e.mutable_config()->CopyFrom(config_);
    entries->push_back(config_e);
    if (pending_config_.has_term() && (!waiting_commits_.size() || // If it isn't in the waiting_commits.
                                       waiting_commits_.front()->index() > pending_config_.index())) {
      LogEntry pending_e;
      pending_e.set_term(pending_config_.term());
      pending_e.set_index(pending_config_.index());
      pending_e.mutable_config()->CopyFrom(pending_config_);
      entries->push_back(pending_e);
    }
    if (uncommitted)
      for (auto &e : waiting_commits_)
        entries->push_back(*e);
  }

  virtual void
  Stop()
  {
    Abdicate();
  }

private:
  struct NodeState {
    int64_t term           = -1;
    int64_t sent_term      = 0;
    int64_t sent_index     = 0;
    int64_t last_log_term  = -1;
    int64_t last_log_index = -1;
    double ack_received    = -1.0e10;
    ::std::string vote;
  };

  Message
  InitializeMessage()
  {
    Message m;
    m.set_term(term_);
    m.set_last_log_term(last_log_term_);
    m.set_last_log_index(index_);
    m.set_from(node_);
    m.set_leader(leader_);
    m.set_data_committed(data_committed_);
    m.set_config_committed(config_committed_);
    return m;
  }

  void
  NewTerm(int64_t term, const ::std::string new_leader, bool in_recovery)
  {
    vote_   = "";
    term_   = term;
    leader_ = new_leader;
    waiting_commits_.clear();
    if (!in_recovery) {
      WriteInternalLogEntry();
      server_->LeaderChange(this, leader_);
    }
  }

  void
  VoteForMe()
  {
    if (seen_term_ || leader_ != "" || vote_ != node_) {
      vote_ = node_;
      term_++;
      leader_ = "";
      waiting_commits_.clear();
      WriteInternalLogEntry();
      server_->LeaderChange(this, leader_);
      seen_term_ = false;
    }
    Vote();
  }

  void
  Vote()
  {
    Message m(InitializeMessage());
    m.set_vote(vote_);
    if (vote_ == node_)
      SendToReplicas(m);
    else
      server_->SendMessage(this, vote_, m);
  }

  void
  HandleVote(const Message &m, NodeState *n)
  {
    n->vote = m.vote();
    if (vote_.empty()) {       // I have not voted yet.
      if (m.vote() == node_) { // Abdication.
        VoteForMe();
      } else if (m.last_log_term() >= last_log_term_ && m.last_log_index() >= index_) {
        // Vote for candidate if it is at least as up to date as we are.
        vote_ = m.vote();
        WriteInternalLogEntry();
        Vote();
      }
    } else if (vote_ == node_ && node_ == n->vote) {
      int votes = 0;
      for (auto &o : other_config_nodes_) {
        auto &s = node_state_[o];
        if (s.term == term_ && s.vote == node_)
          votes++;
      }
      if (votes + 1 > (other_config_nodes_.size() + 1) / 2) {
        leader_ = node_;
        WriteInternalLogEntry();
        server_->LeaderChange(this, leader_);
        HeartBeat(); // Inform the others.
      }
    }
  }

  void
  Ack(bool ack)
  {
    Message m(InitializeMessage());
    if (!ack) { // Reset local log state to last committed.
      m.set_nack(true);
      m.set_last_log_term(last_log_committed_term_);
      m.set_last_log_index(last_log_committed_index_);
      index_         = last_log_committed_index_;
      last_log_term_ = last_log_committed_term_;
    }
    server_->SendMessage(this, leader_, m);
  }

  void
  HandleAck(double now, const Message &m, NodeState *n)
  {
    n->ack_received = now;
    if (m.nack()) {
      n->sent_index = n->last_log_index;
      n->sent_term  = n->last_log_term;
    } else if (i_am_leader()) {
      int acks_needed = (other_nodes_.size() + 1) / 2;
      for (auto &o : other_nodes_)
        if (node_state_[o].ack_received >= last_heartbeat_sent_)
          acks_needed--;
      if (acks_needed <= 0)
        last_heartbeat_ = now;
      UpdateCommitted();
    }
  }

  void
  HeartBeat()
  {
    Message m(InitializeMessage());
    SendToReplicas(m);
  }

  void
  SendToReplicas(const Message &m)
  {
    for (auto &n : replicas_)
      server_->SendMessage(this, n, m);
  }

  void
  Abdicate()
  {
    if (!i_am_leader())
      return;
    // Attempt to pass leadership to a worthy successor.
    const ::std::string *best_node = nullptr;
    NodeState *best                = nullptr;
    for (auto &n : other_nodes_) {
      auto &s = node_state_[n];
      if (!best || (s.last_log_term > best->last_log_term ||
                    (s.last_log_term == best->last_log_term && s.last_log_index > best->last_log_index))) {
        best_node = &n;
        best      = &s;
      }
    }
    if (best_node) {
      term_++;
      leader_ = "";
      vote_   = *best_node;
      WriteInternalLogEntry();
      Message m(InitializeMessage());
      m.set_vote(vote_);
      server_->SendMessage(this, vote_, m);
    }
  }

  void
  WriteInternalLogEntry()
  {
    LogEntry e;
    e.set_term(term_);
    e.set_leader(leader_);
    e.set_vote(vote_);
    e.set_data_committed(data_committed_);
    e.set_config_committed(config_committed_);
    server_->WriteLogEntry(this, e);
  }

  bool
  ProcessLogEntry(const LogEntry &e, bool in_recovery)
  {
    if (e.has_config()) {
      pending_config_.CopyFrom(e.config());
      pending_config_.set_term(e.term());
      pending_config_.set_index(e.index());
      ConfigChanged();
    }
    if (e.has_index()) { // Not an internal entry.
      std::unique_ptr<LogEntry> entry(new LogEntry(e));
      if (e.index() <= index_)
        return true;            // Already seen this.
      if (!entry->has_term()) { // Summary, fill in the internal bits.
        entry->set_term(term_);
        index_ = entry->index() - 1; // Summary need not have an extent().
        entry->set_previous_log_term(last_log_term_);
        entry->set_previous_log_index(index_);
      }
      if (e.term() < last_log_term_)
        return true; // Already seen this.
      if (e.term() == last_log_term_ && e.index() <= index_)
        return true;
      if ((entry->previous_log_term() != last_log_term_ || entry->previous_log_index() != index_))
        return false; // Out of sequence.
      if (last_log_term_ == entry->term() && entry->index() != index_ + 1)
        return false; // Out of sequence.
      last_log_term_ = entry->term();
      index_         = entry->index() + entry->extent();
      if (!in_recovery && i_am_leader()) {
        if (!other_nodes_.size())
          data_committed_ = index_;
        if (!other_config_nodes_.size())
          config_committed_ = index_;
      }
      entry->set_data_committed(data_committed_);
      entry->set_config_committed(config_committed_);
      if (!in_recovery)
        server_->WriteLogEntry(this, *entry);
      waiting_commits_.emplace_back(entry.release());
    }
    return true;
  }

  int
  MajorityIndex(const ::std::set<::std::string> &other)
  {
    ::std::vector<int64_t> indices(1, index_);
    for (auto &o : other)
      indices.push_back(node_state_[o].last_log_index);
    sort(indices.begin(), indices.end());
    return indices[indices.size() / 2];
  }

  void
  UpdateCommitted()
  {
    int i = MajorityIndex(other_nodes_);
    if (i > data_committed_) {
      data_committed_ = i;
      WriteInternalLogEntry();
      Commit(false);
      HeartBeat();
    }
    if (pending_config_.has_term()) { // If a pending configuration change.
      int ci = MajorityIndex(other_config_nodes_);
      // config_committed must be <= data_committed, so the new
      // configuration must also concur with the new data_committed.
      if (i == ci && ci > config_committed_) {
        config_committed_ = ci;
        WriteInternalLogEntry();
        Commit(false);
        HeartBeat();
        if (!i_am_leader() && other_nodes_.size() > 1)
          Abdicate();
      }
    }
  }

  void
  Commit(bool in_recovery)
  {
    ::std::vector<std::unique_ptr<LogEntry>> pending;
    while (!waiting_commits_.empty() && waiting_commits_.front()->index() <= data_committed_) {
      auto &e = waiting_commits_.front();
      while (!pending.empty() && e->index() <= pending.back()->index())
        pending.pop_back();
      pending.emplace_back(e.release());
      waiting_commits_.pop_front();
    }
    for (auto &e : pending) {
      server_->CommitLogEntry(this, *e);
      last_log_committed_term_  = e->term();
      last_log_committed_index_ = e->index();
    }
    CommitConfig(in_recovery);
  }

  void
  CommitConfig(bool in_recovery)
  {
    if (pending_config_.has_term() && pending_config_.term() == term_ && pending_config_.index() <= config_committed_) {
      config_.Swap(&pending_config_);
      pending_config_.Clear();
      server_->ConfigChange(this, config_);
      if (ConfigChanged()) {
        NewTerm(term_ + 1, leader_, in_recovery);
        if (!in_recovery)
          HeartBeat();
      }
    }
  }

  bool
  ConfigChanged()
  { // Returns: true if the leader_ changed.
    other_nodes_.clear();
    other_config_nodes_.clear();
    replicas_.clear();
    for (auto &n : config_.node())
      if (n != node_) {
        other_nodes_.insert(n);
        other_config_nodes_.insert(n);
      }
    for (auto &n : pending_config_.node())
      if (n != node_)
        other_config_nodes_.insert(n);
    replicas_.insert(config_.replica().begin(), config_.replica().end());
    replicas_.insert(pending_config_.replica().begin(), pending_config_.replica().end());
    replicas_.insert(other_nodes_.begin(), other_nodes_.end());
    replicas_.insert(other_config_nodes_.begin(), other_config_nodes_.end());
    ::std::string old_leader = leader_;
    if (!other_nodes_.size())
      leader_ = node_;
    else if (!i_am_in_nodes() && other_nodes_.size() == 1)
      leader_ = *other_nodes_.begin();
    else if (leader_ == node_ && !i_am_in_nodes())
      leader_ = "";
    return leader_ != old_leader;
  }

  bool
  SendReplicationMessage(const ::std::string &n, const LogEntry &entry, NodeState *s)
  {
    Message m(InitializeMessage());
    m.mutable_entry()->CopyFrom(entry);
    if (!server_->SendMessage(this, n, m))
      return false;
    s->sent_index = entry.index() + entry.extent();
    s->sent_term  = entry.term();
    return true;
  }

  void
  Replicate(const ::std::string &n, bool heartbeat)
  {
    bool sent = false;
    auto &s   = node_state_[n];
    if (s.term == term_) { // Replica has acknowledged me as leader.
      int64_t end = index_;
      if (waiting_commits_.size())
        end = waiting_commits_.front()->index() - 1;
      while (s.sent_index < end) { // Get from server.
        LogEntry entry;
        server_->GetLogEntry(this, s.sent_term, s.sent_index + 1, end, &entry);
        if (!entry.has_term()) {
          // A summary log entry from the server with historical information.
          entry.set_term(last_log_term_);
          entry.set_index(s.sent_index + 1);
        }
        entry.set_previous_log_term(s.sent_term);
        entry.set_previous_log_index(s.sent_index);
        assert(entry.index() > s.sent_index);
        int64_t x = s.sent_index;
        if (!SendReplicationMessage(n, entry, &s))
          break;
        assert(s.sent_index > x);
        sent = true;
      }
      for (auto &e : waiting_commits_) {
        if (e->index() <= s.sent_index) // Skip those already sent.
          continue;
        if (!SendReplicationMessage(n, *e, &s))
          break;
        sent = true;
      }
    }
    if (heartbeat && !sent) {
      Message m(InitializeMessage());
      server_->SendMessage(this, n, m);
    }
  }

  void
  ReplicateAll(bool heartbeat)
  {
    for (auto &n : replicas_)
      Replicate(n, heartbeat);
  }

  bool
  i_am_leader()
  {
    return node_ == leader_;
  }
  bool
  i_am_in_nodes()
  {
    auto &n = config_.node();
    return std::find(n.begin(), n.end(), node_) != n.end();
  }

  Server *const server_;
  struct drand48_data rand_;
  const ::std::string node_;
  int64_t term_                     = 0;  // Current term.
  int64_t last_log_term_            = -1; // Term of last log entry this node has.
  int64_t index_                    = 0;  // Index of last log entry this node has.
  int64_t config_committed_         = -1;
  int64_t data_committed_           = -1;
  int64_t last_log_committed_index_ = -1;
  int64_t last_log_committed_term_  = -1;
  double election_timeout_          = 1.0;
  double last_heartbeat_            = -1.0e10;
  double last_heartbeat_sent_       = -1.0e10;
  double random_election_delay_     = 0.0;
  ::std::string leader_; // The current leader.  "" if there is no leader.
  ::std::string vote_;   // My vote this term.
  Config config_;
  Config pending_config_;
  ::std::map<::std::string, NodeState> node_state_;
  ::std::deque<std::unique_ptr<LogEntry>> waiting_commits_;
  bool seen_term_ = true;
  // Cached values.
  ::std::set<::std::string> other_nodes_;        // Nodes required for consensus on log entries.
  ::std::set<::std::string> other_config_nodes_; // Nodes required for config changes.
  ::std::set<::std::string> replicas_;           // All nodes receiving the replication stream.
};

template <typename Server>
Raft<Server> *
NewRaft(Server *server, const ::std::string &node)
{
  return new RaftImpl<Server>(server, node);
}
} // namespace raft
#endif // CONSENSUS_IMPL_H_
