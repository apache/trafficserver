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


#ifndef _P_BC_OpenDirTable_H_
#define _P_BC_OpenDirTable_H_

class BC_OpenDir;
class BlockCacheKey;

/**
  The way OpenDir entries are found and created by rest of cache system.
*/

class BC_OpenDirTable:public Continuation
{
public:
  /// constructor
  BC_OpenDirTable();
  /// destructor
  virtual ~ BC_OpenDirTable();
  /**
    Return BC_OpenDir entry for key.
    
    if found in table, then return it.

    if found, but in process of being removed, or if not found in
    table, then create new BC_OpenDir for key, insert into table and
    return it.
    
    @param key desired BlockCacheKey of new or existing entry
    @return newly created or existing BC_OpenDir
    
    */
  BC_OpenDir *lookupOrCreateOpenDir(BlockCacheKey * key);
private:
};
#endif
