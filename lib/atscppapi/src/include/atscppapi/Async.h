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

#pragma once
#ifndef ATSCPPAPI_ASYNC_H_
#define ATSCPPAPI_ASYNC_H_
#include <list>
#include <atscppapi/Mutex.h>
#include <atscppapi/noncopyable.h>
#include <atscppapi/shared_ptr.h>

namespace atscppapi
{
/**
 * @private
 *
 * @brief This class represents the interface of a dispatch controller. A dispatch controller
 * is used to dispatch an event to a receiver. This interface exists so that the types in this
 * header file can be defined.
 */
class AsyncDispatchControllerBase : noncopyable
{
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

  virtual ~AsyncDispatchControllerBase() {}
};

/**
 * @brief AsyncProvider is the interface that providers of async operations must implement.
 * The system allows decoupling of the lifetime/scope of provider and receiver objects. The
 * receiver object might have expired before the async operation is complete and the system
 * handles this case. Because of this decoupling, it is the responsibility of the provider
 * to manage it's expiration - self-destruct on completion is a good option.
 */
class AsyncProvider
{
public:
  /**
   * This method is invoked when the async operation is requested. This call should be used
   * to just start the async operation and *not* block this thread. On completion,
   * getDispatchController() can be used to invoke the receiver.
   */
  virtual void run() = 0;

  /** Base implementation just breaks communication channel with receiver. Implementations
   * should add business logic here. */
  virtual void
  cancel()
  {
    if (dispatch_controller_) {
      dispatch_controller_->disable();
    }
  }

  virtual ~AsyncProvider() {}
protected:
  shared_ptr<AsyncDispatchControllerBase>
  getDispatchController()
  {
    return dispatch_controller_;
  }

private:
  shared_ptr<AsyncDispatchControllerBase> dispatch_controller_;
  void
  doRun(shared_ptr<AsyncDispatchControllerBase> dispatch_controller)
  {
    dispatch_controller_ = dispatch_controller;
    run();
  }
  friend class Async;
};

/**
 * @private
 *
 * @brief Dispatch controller implementation. When invoking the receiver, it verifies that the
 * receiver is still alive, locks the mutex and then invokes handleAsyncComplete().
 */
template <typename AsyncEventReceiverType, typename AsyncProviderType>
class AsyncDispatchController : public AsyncDispatchControllerBase
{
public:
  bool
  dispatch()
  {
    bool ret = false;
    ScopedSharedMutexLock scopedLock(dispatch_mutex_);
    if (event_receiver_) {
      event_receiver_->handleAsyncComplete(static_cast<AsyncProviderType &>(*provider_));
      ret = true;
    }
    return ret;
  }

  void
  disable()
  {
    ScopedSharedMutexLock scopedLock(dispatch_mutex_);
    event_receiver_ = NULL;
  }

  bool
  isEnabled()
  {
    return (event_receiver_ != NULL);
  }

  /**
   * Constructor
   *
   * @param event_receiver The async complete event will be dispatched to this receiver.
   * @param provider Async operation provider that is passed to the receiver on dispatch.
   * @param mutex Mutex of the receiver that is locked during the dispatch
   */
  AsyncDispatchController(AsyncEventReceiverType *event_receiver, AsyncProviderType *provider, shared_ptr<Mutex> mutex)
    : event_receiver_(event_receiver), dispatch_mutex_(mutex), provider_(provider)
  {
  }

  virtual ~AsyncDispatchController() {}
public:
  AsyncEventReceiverType *event_receiver_;
  shared_ptr<Mutex> dispatch_mutex_;

private:
  AsyncProviderType *provider_;
};

/**
 * @private
 *
 * @brief A promise is used to let the dispatch controller know if the receiver is still
 * alive to receive the async complete dispatch. When the receiver dies, this promise is
 * broken and it automatically updates the dispatch controller.
 */
template <typename AsyncEventReceiverType, typename AsyncProviderType> class AsyncReceiverPromise : noncopyable
{
public:
  AsyncReceiverPromise(shared_ptr<AsyncDispatchController<AsyncEventReceiverType, AsyncProviderType>> dispatch_controller)
    : dispatch_controller_(dispatch_controller)
  {
  }

  ~AsyncReceiverPromise()
  {
    ScopedSharedMutexLock scopedLock(dispatch_controller_->dispatch_mutex_);
    dispatch_controller_->event_receiver_ = NULL;
  }

protected:
  shared_ptr<AsyncDispatchController<AsyncEventReceiverType, AsyncProviderType>> dispatch_controller_;
};

/**
 * @brief AsyncReceiver is the interface that receivers of async operations must implement. It is
 * templated on the type of the async operation provider.
 */
template <typename AsyncProviderType> class AsyncReceiver : noncopyable
{
public:
  /**
   * This method is invoked when the async operation is completed. The
   * mutex provided during the creation of the async operation will be
   * automatically locked during the invocation of this method.
   *
   * @param provider A reference to the provider which completed the async operation.
   */
  virtual void handleAsyncComplete(AsyncProviderType &provider) = 0;
  virtual ~AsyncReceiver() {}
protected:
  AsyncReceiver() {}
  friend class Async;

private:
  mutable std::list<shared_ptr<AsyncReceiverPromise<AsyncReceiver<AsyncProviderType>, AsyncProviderType>>> receiver_promises_;
};

/**
 * @brief This class provides a method to create an async operation.
 */
class Async : noncopyable
{
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
  template <typename AsyncProviderType>
  static void
  execute(AsyncReceiver<AsyncProviderType> *event_receiver, AsyncProviderType *provider, shared_ptr<Mutex> mutex)
  {
    if (!mutex.get()) {
      mutex.reset(new Mutex(Mutex::TYPE_RECURSIVE));
    }
    shared_ptr<AsyncDispatchController<AsyncReceiver<AsyncProviderType>, AsyncProviderType>> dispatcher(
      new AsyncDispatchController<AsyncReceiver<AsyncProviderType>, AsyncProviderType>(event_receiver, provider, mutex));
    shared_ptr<AsyncReceiverPromise<AsyncReceiver<AsyncProviderType>, AsyncProviderType>> receiver_promise(
      new AsyncReceiverPromise<AsyncReceiver<AsyncProviderType>, AsyncProviderType>(dispatcher));
    event_receiver->receiver_promises_.push_back(receiver_promise); // now if the event receiver dies, we're safe.
    provider->doRun(dispatcher);
  }
};
}

#endif /* ATSCPPAPI_ASYNC_H_ */
