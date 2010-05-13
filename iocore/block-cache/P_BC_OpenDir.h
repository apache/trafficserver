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


#ifndef _P_OpenDir_H_
#define _P_OpenDir_H_

/**
  Active directory entry.  Documents which are actively being read or
  written have an active directory.


  What's in the active directory?
    - Pointer to the partition where the document resides/should reside.
    - The directory entry itself.
    - The cache key of the document.
    - Vector of keys for document segments (do these create OpenDir entries too? Only when opened)

  This needs to interact with Dir, and also keep list of
  BC_OpenSegments.

  */
class BC_OpenDir:public Continuation
{
public:
  /// constructor
  BC_OpenDir();
  /// destructor
  virtual ~ BC_OpenDir();


  /// How data will be accessed in the segment
  enum AccessType
  { e_for_read, e_for_write, e_for_hot_write, e_for_remove };

  /**
    Return BC_OpenSegment entry for key.

    Assumptions: synchronous access to Dir.

    When considering each operation (AccessType), we have to consider
    the following situations:

    <ul>
    <li> A. no BC_OpenSegment, no Dir entry exists.
    <li> B. no BC_OpenSegment, a Dir entry exists (because of hash
    collisions, this may not be the correct Dir entry for the key)
    <li> C. BC_OpenSegment with writer, no Dir entry exists
    <li> D. BC_OpenSegment with writer, Dir entry exists (i.e. a segment is being overwritten)
    <li> E. BC_OpenSegment with hot-writer (unabortable writer), no Dir entry exists
    <li> F. BC_OpenSegment with hot-writer (unabortable writer), Dir entry exists (i.e. a segment is being overwritten)
    <li> G. BC_OpenSegment with only readers
    </ul>

    How do we deal with known hash collisions, i.e. that even though Dir
    exists, it isn't the right one?  Could factor that into situations
    above...


    <pre>
    operation x situation:
    e_for_read
      E,F,G. return segment
      D. return new BC_OpenSegment pointing to existing Dir entry.
      B. create and return new BC_OpenSegment pointing to existing Dir entry
      A,C. fail

    e_for_write
      G. create and return new BC_OpenSegment referring to existing
         BC_OpenSegment's Dir entry to be overwritten.  A new Dir entry
	 will be created when the write is done.
      C,D,E,F. fail
      A. create and return new BC_OpenSegment.  A new Dir entry will
         be created when the write is done.
      B. create and return new BC_OpenSegment pointing to existing Dir
         entry.  If it is verified that doc does match the key,
         existing Dir entry will be kept to refer to in overwrite,
         otherwise Dir entry is ignored.


    e_for_hot_write
       same as e_for_write, but new BC_OpenSegment is marked as
       e_for_hot_write.

    e_for_remove
       A. fail
       B. create and return BC_OpenSegment pointing to existing Dir
          entry, however caller will need to wait for key to be
          verified before calling BC_OpenSegment::remove().

       D,F. mark existing BC_OpenSegment as to be removed, and return
          BC_OpenSegment pointing to existing Dir entry.  However,
          caller will need to wait for key to be verified before
          calling BC_OpenSegment::remove.   Storage for existing
          BC_OpenSegment will not go away until readers have finished.

       C,E,G. return existing BC_OpenSegment. BC_OpenSegment::remove()
          can be called on this immediately.  Storage for existing
          BC_OpenSegment will not go away until readers have finished.


    </pre>

    @param key desired BlockCacheKey of new or existing entry
    @param access_type mode of access to the data.
    @return BC_OpenSegment* newly created or existing BC_OpenSegment

    */
  BC_OpenSegment *lookupOrCreateOpenSegment(BlockCacheKey * key, AccessType access_type);

private:
  /// open segments that are part of this document
    Queue<BC_OpenSegment> m_segments;
};
#endif
