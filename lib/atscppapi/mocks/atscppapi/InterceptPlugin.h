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
 * @file InterceptPlugin.h
 */

#pragma once
#ifndef ATSCPPAPI_INTERCEPT_PLUGIN_H_
#define ATSCPPAPI_INTERCEPT_PLUGIN_H_

#include <string>
#include <atscppapi/Transaction.h>
#include <atscppapi/TransactionPlugin.h>

namespace atscppapi {



/**
 * Allows a plugin to act as a server and return the response. This
 * plugin can be created in read request headers hook (pre or post
 * remap).
 */
class InterceptPlugin : public TransactionPlugin {
protected:
  /**
   * The available types of intercepts.
   */
  enum Type {
    SERVER_INTERCEPT = 0, /**< Plugin will act as origin */
    TRANSACTION_INTERCEPT /**< Plugin will act as cache and origin (on cache miss) */
  };

  /** a plugin must implement this interface, it cannot be constructed directly */
  InterceptPlugin(Transaction &transaction, Type type)
  	  : TransactionPlugin(transaction)
  { }


public:
  enum RequestDataType {
    REQUEST_HEADER = 0,
    REQUEST_BODY
  };

  /**
   * A method that you must implement when writing an InterceptPlugin, this method will be
   * invoked whenever client request data is read.
   */
  virtual void consume(const std::string &data, RequestDataType type) = 0;

  /**
   * A method that you must implement when writing an InterceptPlugin, this method
   * will be invoked when the client request is deemed complete.
   */
  virtual void handleInputComplete() = 0;

  /** Should be called only after request header has completely been consumed */
  MOCK_METHOD0(getRequestHeaders, Headers& ());

  virtual ~InterceptPlugin() { }


//protected:
  /**
   * This method is how an InterceptPlugin will send output back to
   * the client.
   */
  //bool produce(const void *data, int data_size);
  MOCK_METHOD2(produce, bool(const void*, int));

  //bool produce(const std::string &data) { return produce(data.data(), data.size()); }
  MOCK_METHOD1(produce, bool(const std::string&));

  //bool setOutputComplete();
  MOCK_METHOD0(setOutputComplete, bool());

private:
  //bool doRead();
  MOCK_METHOD0(doRead, bool());

  //void handleEvent(int, void *);
  MOCK_METHOD2(handleEvent, void(int, void *));
};

} /* atscppapi */


#endif /* ATSCPPAPI_INTERCEPT_PLUGIN_H_ */
