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


#ifndef _P_BC_OpenSegment_H_
#define _P_BC_OpenSegment_H_

#include "I_EventSystem.h"

class BlockCacheKey;
class BlockCacheDir;
class BC_OpenDir;

/**
  Active segment.  Segment which is being actively read or written
  will have an active segment.  Finest granularity of write exclusion
  is at the level of segment.

  What's in the active segment?
    - Pointer back to the active directory.
    - The cache key of the segment.
    - The directory entry of the segment.
    - Cache VConnection which is the writer.
    - whether writer is non-abortable.
    - VIO of writer.
    - Cache VConnection(s) which are the readers.
    - VIO of each reader.

*/
class BC_OpenSegment:public Continuation
{
public:
  /// keeping track of these on OpenDir structure
  Link<BC_OpenSegment> opendir_link;

  /// Continuation event values
  enum EventType
  {
    e_doc_matches = BLOCK_CACHE_EVENT_EVENTS_START + 20,
    e_doc_collision,
    e_closed,
    e_removed,
    e_synced
  };
  /// constructor
    BC_OpenSegment();
  /// destructor
    virtual ~ BC_OpenSegment();

  /**
    Initialize segment

    @param parent - BC_OpenDir that this segment is part of.
    @param key - key that this segment will be using.  A copy is made,
    so the caller can free it afterwards.
    @param dir - directory entry that we think corresponds to key.

    */
  virtual void init(BC_OpenDir * parent, BlockCacheKey * key, BlockCacheDir * dir);

  /**
    Get key that this segment refers to.
    Return value cannot be modified by caller.
    */
  virtual const BlockCacheKey *getKey();

  /**
    Verify the cache key matches document/segment.


    callback with e_doc_matches or e_doc_collision depending on
    whether document is actually the key for the cache or not.  If
    not, then caller will need to go back to the directory and iterate
    through Dir entries in the collision chain.
    <br>
    Why there instead of here?  Trying to keep this object behavior
    simple.


    @param c caller
    @return Action* Cancelling this cancels the callback, but doesn't
    cancel I/O that may have been initiated by this.

    */
  Action *verifyKey(Continuation * c);

  /**
    Put Dir entry back into table and log.

    @param c caller
    @return Action* Cancelling this cancels the callback, but doesn't
    cancel the Dir update.

    */
  Action *close(Continuation * c);

  /**
    Remove associated Dir entry

    @param c caller
    @return Action* Cancelling this cancels the callback, but doesn't
    cancel the remove.
    
    */
  Action *remove(Continuation * c);

  /**
    Wait for directory log to write out.
    
    @param c caller
    @return Action* Cancelling this cancels the callback, but doesn't
    cancel the syncing of directory and log to disk.
    
    */
  Action *sync(Continuation * c);

  /**
   register BlockVConnection as writer
   
   @param vc 

  */
  void registerWriter(BlockCacheSegmentVConnection * vc);

  /**
   register BlockVConnection as reader
   
   @param vc 

  */
  void registerReader(BlockCacheSegmentVConnection * vc);

  /**
     inform that we have space available.
     The reentrancy results from possibly calling caller back with
     more data.
  */
  void readSpaceAvail_re(int amount);

  /**
     inform that we have data available.
  */
  void writeDataAvail(int amount);

private:
};

#endif
