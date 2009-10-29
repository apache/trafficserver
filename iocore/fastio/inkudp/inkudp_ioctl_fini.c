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

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/stream.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/socket.h>
#include <sys/int_types.h>
#include <sys/stropts.h>
#include <inet/common.h>
#include <inet/led.h>
#include <inet/ip.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <sys/sunddi.h>
#include "fastio.h"
#include "solstruct.h"
#include "inkudp_sched.h"


/* Inkudp global data */
extern int *bufbaseptr;
extern int active;
extern int blkcount;
extern int blocksize;           /* FIXME: pass this through shared memory */
extern uint16_t *flist0, *flist1, *activefl;
extern int blockbaseptr;
extern int nextflentry;
extern int modopen;
extern kmutex_t freemx;


/*
 *  Handle a INKUDP_FINI ioctl
 */
int
inkudp_ioctl_fini(mblk_t * mp, queue_t * q)
{

  modopen = 0;

/* ack the ioctl */

  cmn_err(CE_CONT, "inkudp_ioctl_fini: %d.\n", *((int *) mp->b_cont->b_rptr));
  inkudp_ioctl_ack(mp, q);

  return 1;


}
