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


#ifndef _I_BlockCache_H_
#define _I_BlockCache_H_

class BlockCacheKey;

/**
    Block cache public API.

    @remarks
      - What should scan interface look like for walking documents?
      - What should QoS (resource usage) adjustment interface look like?
      - What should GC control interface look like?

*/
class BlockCacheProcessor:public Processor
{
  virtual int start(int n_cache_threads = 0 /* cache uses event threads */ );
  void stop();
  /**
    open document for reading/writing. If doesn't exist, create. If
    exists, then ok.
    Callback returns BlockCacheProcessor::e_open_append

    @param cont  Caller
    @param key  BlockCacheKey of the document to be written.
    @return Action* Canceling this cancels callback and the open if
    the callback hasn't already occurred.

   */
  inkcoreapi Action *open_append(Continuation * cont, BlockCacheKey * key);

  /**

    remove document matching cache key from cache. If doesn't exist,
    then do nothing, but return failure code.

    Callback returns BlockCacheProcessor::e_remove or e_remove_failed.

    If document is actively being read or written, then current reads
    and writes are allowed to finish, but future readers will get an
    open_append failure for the key.

    @param cont  Caller
    @param key  BlockCacheKey of the document to be removed.
    @return Action* Canceling this cancels callback, but the removal
    will still happen.

    */
  inkcoreapi Action *remove(Continuation * cont, BlockCacheKey * key);

  /// callback event code
  enum EventType
  {
    e_open_append = BLOCK_CACHE_EVENT_EVENTS_START,
    e_open_append_failed,
    e_remove,
    e_remove_failed
  };
private:
};

#endif
