/** @file

  Private block cache dir declarations

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


#ifndef _P_BlockCacheDir_H_
#define _P_BlockCacheDir_H_

/**
  Single on disk Directory entry.

  possible sizes: 2^12 * size = 4kb, 8kb, ...

*/
struct BlockCacheDir
{
  /**
    Offset into partition in multiples of 4kb page.

    Offset 0 is used to denote "free" or invalid entry.
    */
  unsigned int offset:28;       // 4kb page * 256M = 1TB partition size
  /**
    document is spread across multiple partitions, the same key will
    be used to access vector in other partitions.  On initializing
    OpenDir, we will have to probe other partitions to piece together
    vector.
    */
  unsigned int multipartition:1;
  unsigned int reserved:3;      // app specific data?
  /**
    size of fragment in multiples of 4kb
    */
  unsigned int size:8;          // 4kb * 256 = 1M fragment
  /**

    portion of BlockCacheKey to disambiguate collisions.  It is legal
    for two BlockCacheDir entries to have the same tag.  It just means
    that the first block of the segment on disk needs to be examined
    for the entire cache key.
    */
  unsigned int tag:12;
  /**
    Next BlockCacheKey in bucket.
    */
  unsigned int next:12;
};

/// sizeof(BlockCacheDir)
#define SIZEOF_BLOCKCACHEDIR 8

struct VectorEntry;

/**
  On disk vector (stored in a document segment).
  For HTTP, this is equivalent to the vector of alternates.
  For streaming, this would be the different media tracks.

  This also serves as the sparse streaming document when using the
  app-specific bits.
  */
struct Vector
{
  /// key of Dir entry that points to this vector
  BlockCacheKey key;
  /// # of bits of app specific data per entry;
  int nbits_app_per_entry;
  struct VectorEntry e[1];
};

struct VectorEntry
{
  /// key to Dir for doc fragment
  BlockCacheKey key;
  /// app specific bits here
};

/**
  On disk document header
  */
struct Doc
{
  /// key of Dir entry that points to this Doc (fragment)
  BlockCacheKey key;
};

/**
  On disk partitioned directory + metadata log.

  The idea is that log is written sequentially and along with the log
  write, a portion of the directory is synced to disk.  The write
  occurs when the log fills up or when a timer expires.  The sizing of
  dir and log portions can be adjusted.

  We will pack directories close together in memory and not keep more
  than a few KB of log in memory.  The syncing process requires doing
  two AIOs -- the header & log, the directories and the footer. With
  coordination among the other disk threads, this could be forced to
  be done without seeks.

  Dir+Log area on disk:
  PartitionedDirLog0 PartitionedDirDir0 PartitionedDirLog1 PartitionedDirDir1 ...

  */

struct PartitionedDirLog
{
  /// sequence # of log entry
  int seq_header;
  /// how much of log is valid
  int nvalid_log;
  /// # of directories per partitioned dir
#define NDIRS_PER_PARTITIONEDDIR (60*1024/SIZEOF_DIR)
  /// # of bytes for logging per partitioned dir
#define LOGBYTES_PER_PARTITIONEDDIR ((64*1024) - (NDIRS_PER_PARTITIONEDDIR*SIZEOF_DIR) - 12)
  /// log portion of partitioned dir
  char log[LOGBYTES_PER_PARTITIONEDDIR];
};

struct PartitionedDirDir
{
  /// directory portion of partitioned dir
  struct BlockCacheDir dir[NDIRS_PER_PARTITIONEDDIR];
  /// sequence # of log entry (written at footer)
  int seq_footer;
#ifdef PARTITIONEDDIR_PADDING
  /// padding to block boundary (if necessary)
  char padding[PARTITIONEDDIR_PADDING];
#endif
};

/**
  type tag for PartitionedDirLog log entries
*/
enum LogEntry_t
{
  e_undef = 0,
  e_add_dir = 1,
  e_remove_dir = 2,
};

/**
  On disk (and in-core) log entry for PartitionedDirLog
  */
struct LogEntry
{
  /// what is in log record
  int recordType:2;
  /// padding
  int reserved:30;
  /// matches type of log record
  union
  {
    struct BlockCacheDir oldDir;        // for remove dir
    struct BlockCacheDir newDir;        // for add dir
  } u;
};

/**
  Internal interface for PartitionedDirLog
  */
class DirLog
{
public:
  /**
    constructor
    */
  DirLog();
  /**
    destructor
    */
  virtual ~ DirLog();

  /**
    @param fd - disk partition
    @param offset - starting offset of partition on disk.
    @param size - size of partition
    @param clear
    */
  void init(int fd, int offset, int size, bool clear);

  /// lock for manipulating these entries.
  ProxyMutex *theLock();

  /**
    Get BlockCacheDir entry for cache key.

    <b><i>how to deal with entry being deleted while we're
    probing?</i></b><i>Do the getEntry while holding openDir lock and
    put desired key in OpenDir entry.  If GC wants to free entry, it
    must first look in openDir entry.  Also, GC cannot even access
    entries to be freed unless lock on DirLog is taken.</i>

    @param dirPart - which partition of partitioned directory to look at
    @param bucket - which entry in partitioned directory to look in
    @param key - key of BlockCacheDir entry
    @param dir - returned entry if found
    @param lastEntry - last entry found (for collision chaining)
    @return int !0 if found, 0 if not found
    */
  int getEntry(int dirPart, int bucket, BlockCacheKey * key, BlockCacheDir * dir, BlockCacheDir * lastEntry);

  /**
    Remove BlockCacheDir entry, suitably logging and updating in-core
    directory.
    @param dirPart - which partition of partitioned directory to look at
    @param bucket - which entry in partitioned directory to look in
    @param key
    @param dir - a directory entry which needs to match the one being removed.
    @return int !0 if found, 0 if not found
    */
  int removeEntry(int dirPart, int bucket, BlockCacheKey * key, BlockCacheDir * dir);

  /**
    Replace BlockCacheDir entry with new entry, suitably logging and
    updating in-core directory.

    @param dirPart - which partition of partitioned directory to look at
    @param bucket - which entry in partitioned directory to look in
    @param key
    @param olddir - a directory entry which needs to match the one being updated.
    @param newdir - new value for directory entry.
    @return int !0 if found, 0 if not found
    */
  int updateEntry(int dirPart, int bucket, BlockCacheKey * key, BlockCacheDir * olddir, BlockCacheDir * newdir);

  /**
    Insert BlockCacheDir entry, suitably logging and updating in-core
    directory.

    @param dirPart - which partition of partitioned directory to look at
    @param bucket - which entry in partitioned directory to look in
    @param key
    @param newdir - value of new directory entry.
    @return int !0 if found, 0 if not found
    */

  int insertEntry(int dirPart, int bucket, BlockCacheKey * key, BlockCacheDir * newdir);

private:

  /// write logging data to current log entry
  int addLog(char *data, int len);
  /// how much space available in current entry for writing log
  int logAvail();

  /**
    Mark all of directory partition (both log and directory) as
    write-busy (while we are syncing it to disk).  While the partition
    is busy, we don't allow writes to it so that the data is not
    disturbed.  All logs are written to a new log region.

    Instead of all updaters performing a try-lock polling
    mechanism, we can use a wait-q like to call back when partitions
    has finally become non-busy, or we can batch up updates to the
    side for application after the write.
    */

  void setBusy(int dirPart);
  /**
    Unmark directory partition (both log and directory data) as being
    write-busy after writing to disk.
   */
  void unsetBusy(int dirPart);
  /**
    Which partition is being written currently (along with logging data).
    */
  int partitionToWrite();
  /**
    Move to next partition to be written.  This also advances to next
    log region of the partition.
    */
  void advancePartition();
  /**
    Pointer to current log data region (for writing to disk)
    */
  char *curLogData();
  /**
    Length of current log data region (for writing to  disk). This should be constant.
    */
  int curLogDataLen();
  /**
    Pointer to current directory partition data region (for writing to disk)
    */
  char *curDirPartData();
  /**
    Length of current directory partition data region (for writing to
    disk). This should be constant.
    */
  int curDirPartLen();
};

#endif
