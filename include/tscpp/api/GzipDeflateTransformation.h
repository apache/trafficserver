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
 * @file GzipDeflateTransformation.h
 * @brief Gzip Deflate Transformation can be used to compress content.
 */

// The C++ Plugin API is deprecated in ATS 10, and will be removed in ATS 11.

#pragma once

#include <string>
#include <string_view>
#include "tscpp/api/TransformationPlugin.h"

namespace atscppapi
{
namespace transformations
{
  /**
   * Internal state for Deflate Transformations
   * @private
   */
  struct GzipDeflateTransformationState;

  /**
   * @brief A TransformationPlugin to easily add gzip deflate to your TransformationPlugin chain.
   *
   * The GzipDeflateTransformation is a helper transformation that can be used
   * to easily compress content. For a full example of GzipDeflateTransformation
   * and GzipInflateTransformation see example/cppapi/gzip_transformation/.
   *
   * @note GzipDeflateTransformation DOES NOT set Content-Encoding headers, it is the
   * users responsibility to set any applicable headers.
   *
   * @see GzipInflateTransformation
   */
  class GzipDeflateTransformation : public TransformationPlugin
  {
  public:
    /**
     * A full example of how to use GzipDeflateTransformation and GzipInflateTransformation is available
     * in example/cppapi/gzip_transformation/
     *
     * @param transaction As with any TransformationPlugin you must pass in the transaction
     * @param type because the GzipDeflateTransformation can be used with both requests and responses
     *  you must specify the Type.
     *
     * @see TransformationPlugin::Type
     */
    GzipDeflateTransformation(Transaction &transaction, TransformationPlugin::Type type);

    /**
     * Any TransformationPlugin must implement consume(), this method will take content
     * from the transformation chain and gzip compress it.
     *
     * @param data the input data to compress
     */
    void consume(std::string_view data) override;

    /**
     * Any TransformationPlugin must implement handleInputComplete(), this method will
     * finalize the gzip compression and flush any remaining data and the epilogue.
     */
    void handleInputComplete() override;

    ~GzipDeflateTransformation() override;

  private:
    std::unique_ptr<GzipDeflateTransformationState> state_; /** Internal state for Gzip Deflate Transformations */
  };
} // namespace transformations
} // namespace atscppapi
