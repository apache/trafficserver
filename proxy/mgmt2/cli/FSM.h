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

/***************************************/
/****************************************************************************
 *
 *  Module: Simple Finite State Machine class
 *  Source: CUJ
 *
 ****************************************************************************/

#ifndef _FSM_H
#define _FSM_H

#include "libts.h"
#include "AbsEventHandler.h"

/* A simple queue for FSM events, borrowed from "SimpleQueue.h"
 * NOTE: not-thread safe like the one in "SimpleQueue.h" */
struct FSMQueueEntry
{
  FSMQueueEntry *next;
  void *data;
  FSMQueueEntry *prev;
};

class FSMQueue
{
public:
  /* default constructor/destructor */
  FSMQueue();
  ~FSMQueue();

  void enqueue(void *data /* IN: object to enqueue */ );
  void *dequeue();
//  void  Print(); /* for debugging only */
private:
    FSMQueueEntry * head;       /* head of queue */
  FSMQueueEntry *tail;          /* tail of queue */

    FSMQueue(const FSMQueue & rhs);     /* copy constructor */
    FSMQueue & operator=(const FSMQueue & rhs); /* assignment operator */
};

/* Event structure */
typedef struct StructEvent
{
  int Id;                       /* event identifier */
  void *parameters;             /* event parameters */
} STRUCT_EVENT;


/*
** Finite State Machine class
**/
class FSM
{
private:
  typedef struct
  {
    int source_state;           /* source */
    int destination_state;      /* destination */
    int event;                  /* event to  handle */
    int index;                  /* Index in fcn table */
  } TransitionType;

  TransitionType *myptrArrayTrans;      /*  a pointer to the */
  /* array of transitions */
  int myMaxNumTransitions;      /* max transitions supported  */
  int myCurrentState;           /* current state of the FSM */

  FSMQueue *myQueue;            /* event queue for this FSM */

  AbsEventHandler *myptrHandler;        /* a pointer to the abstract */
  /*  server class */

  /* Insert event into queue */
  void insertInQueue(int inEvent,       /* IN: event to insert */
                     void *inParameters /* IN: data to pass to handler */ );

  /* Search using hash functions */
  int hash_search(int inSourceState,    /* IN: */
                  int inEvent /* IN: */ );

  int hash_index(int inEvent /* IN: */ );

  /* copy constructor and assignment operator are private
   *  to prevent their use */
    FSM(const FSM & rhs);
    FSM & operator=(const FSM & rhs);

  /* No default constructor */
    FSM();

public:
  /* Constructor */
    FSM(AbsEventHandler * inTrans,      /* IN: handle to event handler */
        int inMaxNumTransitions,        /* IN: maximum num of transitions */
        int inInitialState /* IN: starting state */ );

  /* Destructor */
   ~FSM(void);

  int defineTransition(int inSourceState,       /* IN: */
                       int inDestinationState,  /* IN: */
                       int inEvent,     /* IN: */
                       int inIndice /* IN: */ );

  int control(int inEvent,      /* IN: */
              void *inParameters = NULL /* IN: */ );

  void generateEvent(int inEvent,       /* IN: */
                     void *inParameters = NULL /* IN: */ );
};

#endif /* _FSM_H */
