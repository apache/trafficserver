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

/* 
 *
 *   Interfaces in this header file are experimental, undocumented and
 *   are subject to change even across minor releases of Traffic Server.
 *   None of the interfaces in this file are committed to be stable
 *   unless they are migrated to ts/ts.h  If you require stable APIs to
 *   Traffic Server, DO NOT USE anything in this file.
 *
 *   $Revision: 1.3 $ $Date: 2003-06-01 18:36:51 $
 */

#ifndef __INK_API_EXPERIMENTAL_H__
#define __INK_API_EXPERIMENTAL_H__

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */


/* Cache APIs that are not yet fully supported and/or frozen nor complete. */
inkapi INKReturnCode INKCacheBufferInfoGet(INKCacheTxn txnp, INKU64 * length, INKU64 * offset);

inkapi INKCacheHttpInfo INKCacheHttpInfoCreate();
inkapi void INKCacheHttpInfoReqGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj);
inkapi void INKCacheHttpInfoRespGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj);
inkapi void INKCacheHttpInfoReqSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj);
inkapi void INKCacheHttpInfoRespSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj);
inkapi void INKCacheHttpInfoKeySet(INKCacheHttpInfo infop, INKCacheKey key);
inkapi void INKCacheHttpInfoSizeSet(INKCacheHttpInfo infop, INKU64 size);
inkapi int INKCacheHttpInfoVector(INKCacheHttpInfo infop, void *data, int length);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* __INK_API_EXPERIMENTAL_H__ */
