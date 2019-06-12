/**
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

/**
 * @file RemapPlugin.h
 */

#pragma once

#include "tscpp/api/Transaction.h"
#include "tscpp/api/Url.h"
#include "tscpp/api/utils.h"

namespace atscppapi
{
/**
 * @brief Base class that remap plugins should extend.
 */
class RemapPlugin
{
public:
  /**
   * Constructor
   *
   * @param instance_handle The instance_handle argument received in TSRemapInit() should be passed here.
   */
  RemapPlugin(void **instance_handle);

  enum Result {
    RESULT_ERROR = 0,
    RESULT_NO_REMAP,
    RESULT_DID_REMAP,
    RESULT_NO_REMAP_STOP,
    RESULT_DID_REMAP_STOP,
  };

  /**
   * Invoked when a request matches the remap.config line - implementation should perform the
   * remap. The client's URL is in the transaction and that's where it should be modified.
   *
   * @param map_from_url The map from URL specified in the remap.config line.
   * @param map_to_url The map to URL specified in the remap.config line.
   * @param transaction Transaction
   * @param redirect Output argument that should be set to true if the (new) url should be used
   *                 as a redirect.
   *
   * @return Result of the remap - will dictate further processing by the system.
   */
  virtual Result
  doRemap(const Url &map_from_url ATSCPPAPI_UNUSED, const Url &map_to_url ATSCPPAPI_UNUSED,
          Transaction &transaction ATSCPPAPI_UNUSED, bool &redirect ATSCPPAPI_UNUSED)
  {
    return RESULT_NO_REMAP;
  }

  virtual ~RemapPlugin() {}
};
} // namespace atscppapi
