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

/****************************************************************************
 *
 *  Module: Simple Finite State Machine class
 *  Source: CUJ
 *
 ****************************************************************************/

#include "ink_platform.h"
#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

/* local includes */
#include "FSM.h"

//
// Create queue
//
FSMQueue::FSMQueue()
{
  head = NULL;
  tail = NULL;
}                               // end FSMQueue

//
//    destory the queue
//
//    NOTE: Does not attempt to deallocate the entries
//          bucket data points to.
//
FSMQueue::~FSMQueue()
{
  FSMQueueEntry *current = NULL;
  FSMQueueEntry *next = NULL;

  // Delete all the containers in the queue
  // Does not delete data entries.
  current = head;
  while (current != NULL) {
    next = current->next;
    delete current;
    current = next;
  }
}                               // end ~FSMQueue()

//
//  returns the first item
//
void *
FSMQueue::dequeue()
{
  FSMQueueEntry *headEntry = NULL;
  void *data = NULL;            // handle empty case

  if (head) {
    headEntry = head;
    head = head->next;

    if (head == NULL) {         // The queue is now empty
      tail = NULL;
    } else {                    // New head element
      head->prev = NULL;
    }

    data = headEntry->data;
    delete headEntry;
  }

  return data;
}                               // end dequeue()

//
//     adds new item to the tail of the list
//
void
FSMQueue::enqueue(void *data /* IN: object to enqueue */ )
{
  FSMQueueEntry *newEntry = new FSMQueueEntry;

  newEntry->data = data;
  if (tail == NULL) {           // fist item in queue
    tail = newEntry;
    //ink_assert(head == NULL);
    head = newEntry;
    newEntry->next = NULL;
    newEntry->prev = NULL;
  } else {                      // add to tail
    newEntry->prev = tail;
    newEntry->next = NULL;
    tail->next = newEntry;
    tail = newEntry;
  }
}                               // end enqueue()

/*
 * 01/14/99 elam -
 * Commented out because g++ is not happy with iostream.h
//
//   Prints out the queue.  For DEBUG only
//
void
FSMQueue::Print()
{
  FSMQueueEntry* current = head;

  cout << "FSM Queue: " << this << endl;
  while (current != NULL)
    {
      cout << "\tdata: " << current->data << " next: " << current->next << " prev: " << current->prev <<endl;
      current = current->next;
    }
  cout << "-----------------------------------------" << endl;
} // end Print()
 */

/*********************** Finite state machine fcns ************************/

//
// Constructor
//
FSM::FSM(AbsEventHandler * inTrans,     /* IN: handle to event handler */
         int inMaxNumTransitions,       /* IN: maximum num of transitions */
         int inInitialState /* IN: starting state */ )
  :
myQueue(NULL)
{
  myptrHandler = inTrans;

  // Initialize the maximal number of transitions
  myMaxNumTransitions = inMaxNumTransitions;

  // Initialize the current state
  myCurrentState = inInitialState;

  // Initialize the array of transitions
  myptrArrayTrans = 0;
  myptrArrayTrans = new TransitionType[myMaxNumTransitions];
  // Zero out array
  memset(myptrArrayTrans, -1, myMaxNumTransitions * sizeof(TransitionType));

  // Create event Queue for this FSM
  myQueue = new FSMQueue();
}                               // end FSM()

//
// Destructor
//
FSM::~FSM()
{
  if (myptrArrayTrans)
    delete[]myptrArrayTrans;
  if (myQueue)
    delete myQueue;
}                               // end ~FSM()

//
// Insert event onto queue
//
void
FSM::insertInQueue(int inEvent, /* IN: event to insert */
                   void *inParameters /* IN: data to pass to handler */ )
{
  StructEvent *anEvent = NULL;

  if ((anEvent = new StructEvent()) != NULL) {
    anEvent->Id = inEvent;
    if (inParameters) {
      anEvent->parameters = inParameters;
    } else {
      anEvent->parameters = NULL;
    }

    // Append the event to the end of list
    // myList.append(anEvent);
    myQueue->enqueue(anEvent);
  }
}                               // end insertInQueue()


//
// Used to find a place in the transition table to insert
// the defined transition. Returns free index postion if available.
//
int
FSM::hash_search(int inSourceState,     /* IN: */
                 int inEvent /* IN: */ )
{
  int i = 0;
  int where = ((inEvent << 8) + inSourceState) % myMaxNumTransitions;

  // Look for the first available position
  while ((myptrArrayTrans[(where + i) % myMaxNumTransitions].index != -1)
         && (i < myMaxNumTransitions)) {
    i++;
  }

  // Return negative value if the table is full
  if (i >= myMaxNumTransitions) {
    return -1;
  } else {                      // Return free position³s index
    return ((where + i) % myMaxNumTransitions);
  }
}                               // end hash_search()


//
// Used to get transition number(i.e. index in transtion table) for event.
//
int
FSM::hash_index(int inEvent /* IN: */ )
{
  int i = 0;
  int where = ((inEvent << 8) + myCurrentState) % myMaxNumTransitions;

  // Look for the event in transition table
  while ((myptrArrayTrans[(where + i) % myMaxNumTransitions].source_state != myCurrentState
          || myptrArrayTrans[(where + i) % myMaxNumTransitions].event != inEvent)
         && i < myMaxNumTransitions) {
    i++;
  }

  // Return negative value if event not found
  if (i >= myMaxNumTransitions) {
    return -1;
  } else {                      // Return index of event in transition table
    return ((where + i) % myMaxNumTransitions);
  }
}                               // end hash_index()


//
// Defines the transition from source -> destination
// given the event.
//
int
FSM::defineTransition(int inSourceState,        /* IN: */
                      int inDestinationState,   /* IN: */
                      int inEvent,      /* IN: */
                      int inIndice /* IN: */ )
{
  int index;

  // Search free position in the table of transitions
  index = hash_search(inSourceState, inEvent);
  if (index != -1) {
    myptrArrayTrans[index].source_state = inSourceState;
    myptrArrayTrans[index].destination_state = inDestinationState;
    myptrArrayTrans[index].event = inEvent;
    myptrArrayTrans[index].index = inIndice;
  }

  return index;
}                               // end defineTransition()


//
// Control which event handler gets called and then sets the
// next state in the FSM.
//
int
FSM::control(int inEvent,       /* IN: */
             void *inParameters /* IN: */ )
{
  int result = FALSE;
  StructEvent *anEvent = NULL;
  int transition_num;

  // Insert the received event in the insertInQueue
  insertInQueue(inEvent, inParameters);

  // Get first event off queue
  anEvent = (StructEvent *) myQueue->dequeue();
  while (anEvent) {
    // check if it is valid transition
    if ((transition_num = hash_index(anEvent->Id)) >= 0) {
      if (myptrArrayTrans[transition_num].index == -1) {
        // Missing transition - generate an internal error event
        generateEvent(INTERNAL_ERROR);
      } else {
        // Execute an event handler
        result = (myptrHandler->*myptrHandler->functions[myptrArrayTrans[transition_num].index]) (anEvent->parameters);

        // Change the FSM's state
        myCurrentState = myptrArrayTrans[transition_num].destination_state;
      }
    } else {
      // Missing transition - generate an internal error event
      generateEvent(INTERNAL_ERROR);
    }

    // delete processed event
    delete anEvent;

    // pull the next event from the queue
    anEvent = (StructEvent *) myQueue->dequeue();
  }

  return result;
}                               // end control()

//
// Used to generate internal event
//
void
FSM::generateEvent(int inEvent, /* IN: */
                   void *inParameters /* IN: */ )
{
  insertInQueue(inEvent, inParameters);
}                               // end generateEvent()
