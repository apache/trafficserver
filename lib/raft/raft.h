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
#ifndef RAFT_H_
#define RAFT_H_
// An implementation of the RAFT consensus algorithm:
//   https://ramcloud.stanford.edu/raft.pdf
//
// Features:
//   * Leader election
//   * Log replication
//   * Snapshotting
//   * Configuration updates including changing the set of nodes participating.
//   * Resistant to failures (i.e. complete/partial log/message loss).
//
// Servers need to implement the functionality in ExampleRaftServer.  A
// single server may have many Raft objects.
//
// On startup:
//   MyServer server;
//   Raft<MyServer>* raft(NewRaft(&server));
//   Initialize the log with the initial config if this is the first run:
//     create an empty log entry, set the initial config and write it.
//   for (log_entry : log)
//     Recover(log_entry);
//       expect CommitLogEntry calls.
//   raft.Start(now, random_string_to_initialize_random_number_generator);
//     expect ConfigChange() and LeaderChange()
//
// Main loop (executed by the user code till done):
//   Call Tick(now) initially and periodically, e.g. every 25 msecs.
//     now is monontically increasing time in seconds (double)
//   On a message from a node, call Run(message)
//      expect SendMessage(), GetLogEntry(), WriteLogEntry(),
//      CommitLogEntry(), LeaderChange() and ConfigChange() calls.
//      Note: WriteLogEntry() is blocking, so if you are using Stubby, shift
//            to another (non-Stubby) thread before calling raft.Run()
//   On periodic snapshot, compress the log and call GetSnapshot() to
//     get entries with the raft meta data (and optionally uncommitted
//     entries) which should appear at the end of any compressed log/snapshot.
//     If CommitLogEntry() is idempotent the snapshot can be taken incrementally
//     and a conservative log tail can be retained.
// When done call Stop()
//   expect SendMessage() etc. calls.
// delete the Raft object.
//
// On master:
//    Periodically call Propose() with a new log entry.  This may include data
//    or config, neither or both.
//
// Changing the nodes participating:
//   Configuration changes involving changes in the number of nodes require
//   raft from a majority of both the new and old configurations.  Until
//   the configuration change has been accepted, the old quorum can continue
//   to commit log entries, resulting in the config_commmit falling behind
//   the data_commit.  Once both quorums have accepted the new configuration,
//   the next commit will require both quorums and will update both the
//   data_commit and config_commit at which point ConfigChange() will be called
//   and the new configuration will be live.
//
//   This class is thread-unsafe (wrap it with a lock) and not reentrant.
#include <string>
#include <vector>

namespace raft
{
template <typename Server> class Raft
{
public:
  typedef typename Server::Message Message;
  typedef typename Server::LogEntry LogEntry;

  virtual ~Raft() {}
  virtual void SetElectionTimeout(double seconds) = 0; // 1 sec.

  virtual void Recover(const LogEntry &entry) = 0;
  virtual void Start(double now, int64_t random_seed) = 0;
  virtual void Tick(double now)               = 0; // Call every ~election_timeout/10.
  virtual void Propose(const LogEntry &entry) = 0;
  virtual void Run(double now, const Message &message)                      = 0;
  virtual void Snapshot(bool uncommitted, ::std::vector<LogEntry> *entries) = 0;
  virtual void Stop() = 0; // Clean shutdown for faster failover.
};
// The server argument is not owned by the Raft.
template <typename Server> Raft<Server> *NewRaft(Server *server, const ::std::string &node);

// The Server template argument of Raft must conform to this interface.
class RaftServerInterface
{
public:
  typedef Raft<RaftServerInterface> RaftClass;
  class Config;   // See RaftConfigPb in raft.proto.
  class LogEntry; // See RaftLogEntryPb in raft.proto.
  class Message;  // See RaftMessagePb in raft.proto.

  // Since a single server may handle multiple raft objects, the
  // RaftClass argument is provided to differentiate the caller.

  // Send a raft message to the given node.
  // Returns: true if accepted for delivery.
  bool SendMessage(RaftClass *raft, const ::std::string &node, const Message &message);
  // Get a LogEntry to update a node from after (term, index) up to end.  These
  // could be the actual written log entry or for committed log entries one
  // which summarizes the changes.
  void GetLogEntry(RaftClass *raft, int64_t term, int64_t index, int64_t end, LogEntry *entry);
  // Write a log entry, returning when it has been persisted.
  void WriteLogEntry(RaftClass *raft, const LogEntry &entry);
  // Commit a log entry, updating the server state.
  void CommitLogEntry(RaftClass *raft, const LogEntry &entry);
  // The leader has changed.  If leader.empty() there is no leader.
  void LeaderChange(RaftClass *raft, const ::std::string &leader);
  // The configuration has changed.
  void ConfigChange(RaftClass *raft, const Config &config);
};
} // namespace raft
#endif // RAFT_H_
