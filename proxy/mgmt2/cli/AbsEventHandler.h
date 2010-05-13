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
 *  Module: Abstract class for event handling
 *  Source: CUJ
 *
 ****************************************************************************/

#ifndef _ABS_EVENTHANDLER_H
#define _ABS_EVENTHANDLER_H

#include "inktomi++.h"

#define INTERNAL_ERROR	0       /* Internal error event, must define */
                                /* 'application events' > 0 but must */
                                /* have handler for internal error event */
#ifndef FALSE
#define FALSE		0
#define TRUE		1
#endif

/* forward declaration */
class AbsEventHandler;

/* A typedef for a pointer to an event handler function */
typedef bool(AbsEventHandler::*FuncAbsTransition) (void *);

/* Abstract class */
class AbsEventHandler
{
  friend class FSM;             /* Finite State Machine */

public:
  /* No default constructor */

  /* Constructor */
    AbsEventHandler(int inNumberTransitions)
  {                             /* create array of function pointers for transitions */
    functions = new FuncAbsTransition[inNumberTransitions];
  }

  /* destructor */
  virtual ~ AbsEventHandler(void)
  {
    if (functions)
      delete functions;
  };

protected:
  /* A pointer to an array of event handler fcns */
  FuncAbsTransition * functions;

  /* Pure virtual function to redefine in derived application specific class */
  virtual void FillHandlersArray(void) = 0;

private:
  /* copy constructor and assignment operator are private
   *  to prevent their use */
  AbsEventHandler(const AbsEventHandler & rhs);
  AbsEventHandler & operator=(const AbsEventHandler & rhs);
};

#endif /* _ABS_EVENTHANDLER_H */
