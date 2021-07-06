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
 * @file TransformationPlugin.h
 */

#pragma once

#include <string_view>
#include "tscpp/api/Transaction.h"
#include "tscpp/api/TransactionPlugin.h"

namespace atscppapi
{
struct TransformationPluginState;
class Continuation;

/**
 * @brief The interface used when you wish to transform Request or Response body content.
 *
 * Transformations are deceptively simple, transformations are chained so the output
 * of one TransformationPlugin becomes the input of another TransformationPlugin. As
 * data arrives it will fire a consume() and when all the data has been sent
 * you will receive a handleInputComplete(). Data can be sent to the next TransformationPlugin
 * in the chain by calling produce() and when the transformation has no data left to send
 * it will fire a setOutputCompete().
 *
 * Since a TransformationPlugin is a type of TransactionPlugin you can call registerHook() and
 * establish any hook for a Transaction also; however, remember that you must implement
 * the appropriate callback for any hooks you register.
 *
 * A simple example of how to use the TransformationPlugin interface follows, this is an example
 * of a Response transformation, the available options are REQUEST_TRANSFORMATION and RESPONSE_TRANSFORMATION
 * which are defined in Type.
 *
 * This example is a Null Transformation, meaning it will just spit out the content it receives without
 * actually doing any work on it.
 *
 * \code
 * class NullTransformationPlugin : public TransformationPlugin {
 * public:
 *   NullTransformationPlugin(Transaction &transaction)
 *     : TransformationPlugin(transaction, RESPONSE_TRANSFORMATION) {
 *     registerHook(HOOK_SEND_RESPONSE_HEADERS);
 *   }
 *   void handleSendResponseHeaders(Transaction &transaction) {
 *     transaction.getClientResponse().getHeaders().set("X-Content-Transformed", "1");
 *     transaction.resume();
 *   }
 *   void consume(std::string_view data) {
 *     produce(data);
 *   }
 *   void handleInputComplete() {
 *     setOutputComplete();
 *   }
 * };
 * \endcode
 *
 * @see Plugin
 * @see TransactionPlugin
 * @see Type
 * @see HookType
 */
class TransformationPlugin : public TransactionPlugin
{
public:
  /**
   * The available types of Transformations.
   */
  enum Type {
    REQUEST_TRANSFORMATION = 0, /**< Transform the Request body content */
    RESPONSE_TRANSFORMATION,    /**< Transform the Response body content */
    SINK_TRANSFORMATION         /**< Sink transformation, meaning you get a separate stream of the Response
                                     body content that does not get hooked up to a downstream input */
  };

  /**
   * A method that you must implement when writing a TransformationPlugin, this method will be
   * fired whenever an upstream TransformationPlugin has produced output.
   */
  virtual void consume(std::string_view data) = 0;

  /**
   * Call this method if you wish to pause the transformation.
   */
  void pause();

  /**
   * Returns true if the transformation is paused, false otherwise.
   */
  bool isPaused() const;

  /**
   * Returns a reference to the Continuation to schedule to resume (unpause) the transformation.  It will be resumed when this
   * continuation is executed.  Must not call this unless the transformation is paused.
   */
  Continuation &resumeCont();

  /**
   * A method that you must implement when writing a TransformationPlugin, this method
   * will be fired whenever the upstream TransformationPlugin has completed writing data.
   */
  virtual void handleInputComplete() = 0;

  ~TransformationPlugin() override; /**< Destructor for a TransformationPlugin */
protected:
  /**
   * This method is how a TransformationPlugin will produce output for the downstream
   * transformation plugin.
   */
  size_t produce(std::string_view);

  /**
   * This is the method that you must call when you're done producing output for
   * the downstream TransformationPlugin.
   */
  size_t setOutputComplete();

  /** a TransformationPlugin must implement this interface, it cannot be constructed directly */
  TransformationPlugin(Transaction &transaction, Type type);

private:
  TransformationPluginState *state_; /** Internal state for a TransformationPlugin */
  size_t doProduce(std::string_view);
  static int resumeCallback(TSCont cont, TSEvent event, void *edata); /** Resume callback*/
};

} // namespace atscppapi
