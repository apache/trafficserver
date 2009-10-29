/** @file

  Multi-threaded interactor

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

  @section details Details

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#ifndef _I_MTInteractor_H_
#define _I_MTInteractor_H_

#include "I_EventSystem.h"

class MTClient;

/**
  Multi-Thread interaction pattern.

  Similar to way media tunnelling is done where continuations have their
  individual locks, but need to communicate with each other in a thread
  safe way.  The design where all continuations share the same lock
  isn't used here (since that is a specific situation and code such as
  the cache can't assume that specific situation).

  Almost all operations require MTInteractor's lock:
    - iterating over clients
    - updating list of clients
    - updating per-client stat

  Example:
  <pre>
     class C : public MTInteractor {
        ...
      void callAllClients() {
        if (try_lock()) {
            MTClient *c;
	    for (c=m_clients.head; c; c = c->link.next) {
	        if (c->try_lock()) {
		    c->handleEvent(MYCALL,0);
		    c->unlock();
		}
	    }
	    unlock();
        }
      }
    }
  </pre>
*/
class MTInteractor:public Continuation
{
public:
  MTInteractor(ProxyMutex * amutex = NULL);
  virtual ~ MTInteractor();
  /**
    Attach given client to this interactor.  Other clients can then see
    and possibly call it.

    @param client client to attach to this interactor.
    @return int not 0 if successfully attached.

  */
  virtual int attachClient(MTClient * client) = 0;

  /**
    Try to acquire MTInteractor's lock.

    @return int not 0 if successfully locked.

  */
  int try_lock();

  /**
    Release lock.

  */
  void unlock();

  /**
    ONLY used by MTClient implementation. remove given client from this
    interactor.  Other clients can then no longer see or possibly call it.

    @param client to remove from this interactor.
    @return not 0 if successfully detached.

  */
  virtual int detachClient(MTClient * client) = 0;


private:
    Ptr<ProxyMutex> m_lock;
};

/**
  Individual participant in interaction.

  The MTClient is composed of two sets of data under two different locks:
  client owned data under MTClient's lock and MTInteractor owned data
  under MTInteractor's lock.  This is for bookkeeping purposes only
  to avoid putting lots of client-specific data in the MTInteractor
  structure.

  Having part of MTClient owned by MTInteractor reinforces the fact that
  MTClient must detach itself from MTInteractor to avoid being called
  or touched by it.

  Certain operations require lock:
    - performing I/O or calling operation on client.
    - verifying client owned data--same as re-checking condition
      variable's condition under lock.

  Other operations don't:
    - MTInteractor updating data which is owned by its lock
    - checking client owned data, e.g. where if you miss the check the
      first time around, you can always get it the next time around. This
      is like checking the condition without locking the condition
      variable.

  To simplify use of this, override just the two state handlers
  dealing with being in joined state or being in detached state. The
  other handlers are for doing the join/leave locking interaction with
  MTInteractor.

 */
class MTClient:public Continuation { public:
  Link<MTClient> link; MTClient(ProxyMutex * amutex = NULL); virtual ~
  MTClient();

  /**
    Try to acquire MTClient's lock.

    @return not 0 if successfully locked.

  */
  int try_lock();

  /**
    Release lock.

  */
  void unlock();

  virtual void setMTInteractor(MTInteractor * t);
  virtual void unsetMTInteractor();

  /**
    Start process of attaching client to interactor.

    When attach has happened, state will be set to handleAttached()
    and will get event == MTClient::e_attached.

    @param t interactor to attach to
    @return return value for state handler.
    
  */
  virtual int startAttach(MTInteractor * t);

  /**
    Start process of detaching client from its interactor. Other
    clients will then no longer see or possibly call it. When detach has
    happened, state will be set to handleDetached() and will get event
    == MTClient::e_detached.

    @return return value for state handler.

  */
  virtual int startDetach();

  /**
    Handler for attached state. When attach has happened, will get event
    == MTClient::e_attached.

  */
  virtual int handleAttached(int event, void *data);

  /**
    Handler for detached state.
    
  */
  virtual int handleDetached(int event, void *data);

  /**
    Callback event code.

  */
  enum EventType
  {
    e_attached = UTILS_EVENT_EVENTS_START,
    e_detached
  };
private:
  /**
    Intermediate state when trying to attach. 
    
  */
  int handleAttaching(int event, void *data);

  /**
    Intermediate state when trying to detach. 
    
  */
  int handleDetaching(int event, void *data);

protected:
  /** Owned by MTInteractor */
  MTInteractor * m_mti;

private:
  /** Owned by MTClient */
  Ptr<ProxyMutex> m_lock;
  Action *m_join;
  Action *m_leave;
};

#endif
