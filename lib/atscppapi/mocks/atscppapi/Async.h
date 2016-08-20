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
 * @file Async.h
 * @brief Provides constructs to perform async operations.
 */

//#pragma once
#ifndef ATSCPPAPI_ASYNC_H_
#define ATSCPPAPI_ASYNC_H_
#include <list>
#include <atscppapi/Mutex.h>
#include <atscppapi/noncopyable.h>
#include <atscppapi/shared_ptr.h>

namespace atscppapi {

/**
 * @private
 *
 * @brief This class represents the interface of a dispatch controller. A dispatch controller
 * is used to dispatch an event to a receiver. This interface exists so that the types in this
 * header file can be defined.
 */
class AsyncDispatchControllerBase : noncopyable {
public:
  /**
   * Dispatches an async event to a receiver.
   *
   * @return True if the receiver was still alive.
   */
  virtual bool dispatch() = 0;

  /** Renders dispatch unusable to communicate to receiver */
  virtual void disable() = 0;

  /** Returns true if receiver can be communicated with */
  virtual bool isEnabled() = 0;

  virtual ~AsyncDispatchControllerBase() { }
};

/**
 * @brief AsyncProvider is the interface that providers of async operations must implement. 
 * The system allows decoupling of the lifetime/scope of provider and receiver objects. The 
 * receiver object might have expired before the async operation is complete and the system
 * handles this case. Because of this decoupling, it is the responsibility of the provider
 * to manage it's expiration - self-destruct on completion is a good option.
 */
class AsyncProvider {
public:

  virtual void run() = 0;
  MOCK_METHOD0(rin, void());
  virtual ~AsyncProvider() { }

protected:
  MOCK_METHOD0(getDispatchController, shared_ptr<AsyncDispatchControllerBase>());

private:
  shared_ptr<AsyncDispatchControllerBase> dispatch_controller_;

  MOCK_METHOD1(doRun, void (shared_ptr<AsyncDispatchControllerBase>));

  friend class Async;
};

/**
 * @private
 *
 * @brief Dispatch controller implementation. When invoking the receiver, it verifies that the
 * receiver is still alive, locks the mutex and then invokes handleAsyncComplete().
 */
template<typename AsyncEventReceiverType, typename AsyncProviderType>
class AsyncDispatchController : public AsyncDispatchControllerBase {
public:
	MOCK_METHOD0(dispatch, bool());
	MOCK_METHOD0(disable, void());
	MOCK_METHOD0(isEnabled, bool());
	AsyncDispatchController(AsyncEventReceiverType *event_receiver, AsyncProviderType *provider, shared_ptr<Mutex> mutex)
	{ }

  virtual ~AsyncDispatchController() { }

};

/**
 * @private
 * 
 * @brief A promise is used to let the dispatch controller know if the receiver is still
 * alive to receive the async complete dispatch. When the receiver dies, this promise is
 * broken and it automatically updates the dispatch controller.
 */
template<typename AsyncEventReceiverType, typename AsyncProviderType>
class AsyncReceiverPromise : noncopyable {
public:
  AsyncReceiverPromise(shared_ptr<AsyncDispatchController<AsyncEventReceiverType, AsyncProviderType> > dispatch_controller)
  {

  }

  ~AsyncReceiverPromise() {
  }

};

/**
 * @brief AsyncReceiver is the interface that receivers of async operations must implement. It is
 * templated on the type of the async operation provider.
 */
template<typename AsyncProviderType>
class AsyncReceiver : noncopyable {
public:
  /**
   * This method is invoked when the async operation is completed. The
   * mutex provided during the creation of the async operation will be
   * automatically locked during the invocation of this method.
   *
   * @param provider A reference to the provider which completed the async operation.
   */
  virtual void handleAsyncComplete(AsyncProviderType &provider) = 0;
  virtual ~AsyncReceiver() { }
protected:
  AsyncReceiver() { }
  friend class Async;
private:
  mutable std::list<shared_ptr<AsyncReceiverPromise<AsyncReceiver<AsyncProviderType>, AsyncProviderType> > > receiver_promises_;
};

/**
 * @brief This class provides a method to create an async operation.
 */
class Async : noncopyable {
public:
  /**
   * This method sets up the dispatch controller to link the async operation provider and 
   * receiver and then initiates the operation by invoking the provider. 
   *
   * @param event_receiver The receiver of the async complete dispatch.
   * @param provider The provider of the async operation.
   * @param mutex The mutex that is locked during the dispatch of the async event complete.
   *              One will be created if nothing is passed in. Transaction plugins should use 
   *              TransactionPlugin::getMutex() here and global plugins can pass an appropriate
   *              or NULL mutex.
   */
  template<typename AsyncProviderType>

  static void execute(AsyncReceiver<AsyncProviderType> *, AsyncProviderType *, shared_ptr<Mutex>)
  { }
};

}


#endif /* ATSCPPAPI_ASYNC_H_ */
