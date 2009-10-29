/** @file

  A brief file description

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

// This code is currently back burner -- i.e. not used right now.

#ifndef _CacheLog_H_
#define _CacheLog_H_

typedef int int32;
typedef int32 XactId;
typedef int BlockOffset;
typedef char Dir[8];

/**
  Individual log records written to disk
  */
struct LogRecord
{
  /**
    do we need to free this structure (or not).
    */
  unsigned short dynalloc:1;
  /**
    length of the structure that needs to be written including the
    bytes that are used to store this count.  This is also written to
    disk for simplicity of handling.
    */
  unsigned short length:15;

  /// what is in log record
  int recordType:8;
  /// Transaction id if it matters, 0 if not.  See XactLog::XactLogType type.
  int curXact:24;

  /// matches type of log record
  union
  {
    struct
    {
      int nXacts;               // active transactions
      XactId list[1];
    } checkpointStart;
    // no data for checkpointEnd;
    Dir oldDir;                 // for remove dir
    Dir newDir;                 // for add dir
    struct
    {
      BlockOffset offset;
      char oldBlockData[1];     // for rollback of block (if dirty)
    } blockOp;
    // no data for commit or abort transaction
  } u;

};

/// minimum amount of data in a log record (from start of LogRecord through curXact)
#define LOGRECORD_MIN 6
#define LOGRECORD_DIR sizeof(Dir)


/// assumption about what disk i/o write length is atomic
#define DISK_ATOMIC_WRITE_LENGTH 512

/**
  Header for on disk batch of log records
  */
struct LogSectorHeader
{
  /// first in sequence (i.e. header)
  unsigned int first:1;
  /// last in sequence (i.e. footer)
  unsigned int last:1;
  /// reserved for future use
  unsigned int padding1:6;
  /// # of DISK_ATOMIC_WRITE_LENGTH byte sectors
  unsigned int nsectors:8;
  /// reserved for future use
  unsigned int padding2:16;
  /// sequence #
  unsigned int sequence;
};

/// length of remainder of LogSector
#define LOG_SECTOR_DATA_LEN (DISK_ATOMIC_WRITE_LENGTH-sizeof(LogSectorHeader))

/**
  Single sector of on disk log records
  */
struct LogSector
{
  /// header
  struct LogSectorHeader hdr;
  /// data body
  char data[LOG_SECTOR_DATA_LEN];
};

class Action;
class Continuation;

/**
   Tranaction log calls.  This is a processor.

   Currently, the documentation is exposed, but it is intended that
   <strong><em>only</em></strong> implementors of block cache make use
   of these calls. 

 */

class XactLog:public Processor
{
public:
  /// start processor (called by event system)
  virtual int start(int n_threads = 0);
  /// stop procesor
  void stop();

  /**
    write start transaction record for id to log.
    @param id XactId of transaction
    */
  void startXact(XactId id);

  /**
    write commit transaction record for id to log.
    @param id XactId of transaction
    */
  void commitXact(XactId id);

  /**
    write abort transaction record for id to log.
    @param id XactId of transaction
    */
  void abortXact(XactId id);

  /**
    Flush transaction log to disk and callback when done.
    @param c Continuation that will be called back when flush is complete.
    @return Action* that can be cancelled.  This only cancels the
    Callback, not the write.

    */
  Action *flush(Continuation * c);
  typedef enum
  {
    e_undef = 0,
    e_start_checkpoint = 1,
    e_end_checkpoint = 2,
    e_add_dir = 3,
    e_remove_dir = 4,
    e_block_written = 5,
    e_block_dirty = 6,
    e_start_transaction = 7,
    e_commit_transaction = 8,
    e_abort_transaction = 9
  } XactLogType;
private:
  void appendLog(LogRecord *);
  LogRecord *newLog(XactLogType t, void *allocedMem = (void *) 0, int len = 0);
  void freeLog(LogRecord *);
  /// handles flushOp
  OpQueue m_flushOp;
  /// for writing out IOBufferBlocks to log.
  AIOCallback *m_io;

  /// current block being prepared for log
    Ptr<IOBufferBlock> m_current;
  /**
    last block in current chain being prepared for log.  This is
    where new log records are written.
    */
    Ptr<IOBufferBlock> m_last_in_current;
  /// block chain being written to log.
    Ptr<IOBufferBlock> m_writing;
};

#endif
