.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../common.defs

.. default-domain:: cpp

.. highlight:: cpp

.. _threads-and-events:

Threads and Event Processing
****************************

Internally |TS| is a cooperative multi-threaded environment. There are a fixed number of threads for core operations, determined at process start time. All core operations take place on one of these existing threads. Plugins may spawn additional threads but these are outside the scope of this document.

Threads
=======

|TS| has a taxonomy of thread types which are layered to create the threading infrastructure. At the
most basic are threads as the operating system provides. Classes provide additional data and
operations on these threads to make them operate properly for |TS|.

Thread
------

The abstract :class:`Thread` is the base class for thread operations. It contains a mutex and a
thread identifier. The logic for starting the thread at the system level is embedded in this class.
All threads started by |TS| have an instance of this class (or subclass). Plugins can directly start
their own threads via system calls and those are not tracked. :class:`Thread` sets up thread local
storage via :code:`pthread_setspecific`. Threads can be started via an explicit function provided to
:func:`Thread::start` or by subclassing :class:`Thread` and overriding :func:`Thread::execute`.

:class:`Thread` also performs the basic time keeping for |TS|. The class contains a global static value which is treated as the current time for |TS|. Usually this class is accessed as a static but it can also be accessed in a way to update the current time. Because of the large number of threads the static use is generally sufficiently accurate because it contains the last time any thread updated.

EThread
-------

:class:`EThread` is a subclass of :class:`Thread` which provides support for |TS| core operations.
It is this class that provides support for using :class:`Continuation` instances. :class:`EThread`
overrides the :func:`Thread::execute` method to gain control after the underlying thread is started.
This method executes a single continuation at thread start. If the thread is :enumerator:

`ThreadType::DEDICATED` it returns after invoking the start continuation. No join is executed, the
presumption is the start continuation will run until process termination. This mechanism is used
because it is, from the |TS| point of view, the easiest to use because of the common support of
continuations.

A :enumerator:`ThreadType::REGULAR` thread will first execute its start continuation and then process its event queue until explicitly stopped after executing the start continuation.

Despite the name :class:`EventProcessor` is primarily a thread management class. It enables the
creation and management of thread groups which are then used by the |TS| core for different types of
computation. The set of groups is determined at run time via subsystems making calls to the
:func:`EventProcessor::register_event_type`. Threads managed by :class:`EventProcessor` have the
:class:`EThread` start continuation controlled by :class:`EventProcessor`. Each thread group (event
type) has a list of continuations to run when a thread of that type starts. Continuations are added
to the list with :func:`EventProcessor::schedule_spawn`. There are two variants of this method, one
for continuations and one for just a function. The latter creates a continuation to call the
function and then schedules that using the former. The :class:`EventProcessor` internal start
continuation for the :class:`EThread` executes the continuations on this list for the appropriate
thread group and then returns, after which :func:`EThread::execute` loops on processing its event
queue.

:class:`EventProcessor` is intended to be a singleton and the global instance is :var:`eventProcessor`.

In general if a subsystem in the |TS| core is setting up a thread group, it should use code of the
form

.. code-block:: cpp

   int ET_GROUP; // global variable, where "GROUP" is replaced by the actual group / type name.
   int n_group_threads = 3; // Want 3 of these threads by default, possibly changed by configuration options.
   constexpr size_t GROUP_STACK_SIZE = DEFAULT_STACK_SIZE; // stack size for each thread.
   void Group_Thread_Init(EThread*); // function to perform per thread local initialization.

   ET_GROUP = eventProcessor::registerEventType("Group");
   eventProcessor.schedule_spawn(&Group_Per_Thread_Init, ET_GROUP);
   eventProcessor.spawn_event_threads(ET_GROUP, n_group_threads, GROUP_STACK_SIZE);


The function :code:`Group_Thread_Init` can be replaced with a continuation if that's more
convenient. One advantage of a continuation is additional data (via :arg:`cookie`) can be provide
during thread initialization.

If there is no thread initialization needed, this can be compressed in to a single call

.. code-block:: cpp

   ET_GROUP = eventProcessor.spawn_event_threads("Group", n_group_threads, GROUP_STACK_SIZE);

This registers the group name and type, starts the threads, and returns the event type.

Types
=====

.. type:: EventType

   A thread classification value that represents the type of events the thread is expected to process.

.. var:: EventType ET_CALL

   A predefined :type:`EventType` which always exists. This is deprecated, use :var:`ET_NET` instead.

.. var:: EventType ET_NET

   A synonym for :var:`ET_CALL`.

.. var:: EventProcessor eventProcessor

   The global single instance of :class:`EventProcessor`.

.. type:: ThreadFunction

   The type of function invoked by :func:`Thread::start`. It is a function returning :code:`void*` and taking no arguments.

.. class:: Thread

   Wrapper for system level thread.

   .. function:: start(const char * name, void * stack, size_t stacksize, ThreadFunction const &f)

      Start the underyling thread. It is given the name :arg:`name`. If :arg:`stack` is
      :code:`nullptr` then a stack is allocated for it of size :arg:`stacksize`. Once the thread is
      started, :arg:`f` is invoked in the context of the thread if non :code:`nullptr`, otherwise
      the method :func:`Thread::execute` is called. The thread execution returns immediately after
      either of these, leaving a zombie thread. It is presumed both will execute until process
      termination.

   .. function:: void execute()

      A pure virtual method that must be overridden in a subclass.

.. class:: EThread

   Event processing thread.

   .. function:: EventType registerEventType(const char* name)

      Register an event type by name. This reserves an event type index which is returned as :type:`EventType`.

   .. function:: void execute()

      Call the start continuation, if any. If a regular (not dedicated) thread, continuously process the event queue.

.. enum:: ThreadType

   .. enumerator:: DEDICATED

      A thread which executes only the start continuation and then exits.

   .. enumerator:: REGULAR

      A thread which executes the start continuation and then processes its event queue.

.. class:: Continuation

   A future computation. A continuation has a handler which is a class method with a
   specific signature. A continuation is invoked by calling its handler. A future computation can be
   referenced by an :class:`Action` instance. This is used primarily to allow the future work to be
   canceled.

.. class:: Action

   Reference to a future computation for a :class:`Continuation`.

.. class:: Event : public Action

   Reference to code to dispatch. Note that an :class:`Event` is a type of :class:`Action`. This class combines the future computational reference of :class:`Action`

.. type:: ThreadSpawnFunction

   A function that takes a single argument of pointer to :class:`EThread` and returns :code:`void`. The argument will be the :class:`EThread` in which the function is executing.

.. class:: EventProcessor

   .. function:: EventType register_event_type(char const * name)

      Register an event type with the name :arg:`name`. The unique type index is returned.

   .. function:: Event * schedule_spawn(Continuation * c, EventType ev_type, int event = EVENT_IMMEDIATE, void * cookie = nullptr)

      When the :class:`EventProcessor` starts a thread of type :arg:`ev_type`, :arg:`c` will be
      called before any events are dispatched by the thread. The handler for :arg:`c` will be called
      with an event code of :arg:`event` and data pointer of :arg:`cookie`.

   .. function:: Event * schedule_spawn(void ( * f) (EThread * ), EventType ev_type)

      When the :class:`EventProcessor` starts a thread of type :arg:`ev_type` the function :arg:`f`
      will be called with a pointer to the :class:`EThread` instance which is starting.
