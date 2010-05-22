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

/*
 *
 * FastIO Userland Library
 *
 *
 *
 *
 *
 * Stub routines for interacting with FastIO services
 *
 */

#ifndef LIBFASTIO_H
#define LIBFASTIO_H




#ifndef _KERNEL
#if defined(sunos)
#include <thread.h>
#include <synch.h>
#endif
#endif

#if defined(linux)
#include <sys/types.h>
typedef u_int32_t uint32_t;
#endif

#include "fastio.h"





#define FIO_DEV "/dev/inkfio"

#define FASTIO_BALLOC_NO_BLOCK 0x1



/*
 * state cookie for an instance of fastIO
 */
  struct fastIO_state
  {


    int fiofd;

    int size;
    int blockcount;
    int blockbase;
    int blocksize;
    int *buffer;

    int active;
    uint32_t *flist0;
    uint32_t *flist1;
    uint32_t *activefl;
    int nextflentry;
#if defined(sunos)
    /* FIXME: for other platfroms */
    mutex_t mem_mutex;
#endif

    struct fastIO_block *blocks;

  };


#define FASTIO_SESSION_UDP     0x0
#define FASTIO_SESSION_VIRTUAL 0x1

  struct fastIO_session
  {

    int fd;
    int udp_queue;              /* pointer to wput queue */
    int type;
    int vsession_id;
    struct fastIO_state *fio;

  };

#if defined(sunos)

/*
 * Initialize the fastIO system and create a state cookie
  */
  struct fastIO_state *fastIO_init(int blockcount);

/*
 * Create a fastIO UDP session
 */
  struct fastIO_session *fastIO_udpsession_create(struct fastIO_state *cookie, int fd);

/*
 * Create a fastIO virtual session
 */
  struct fastIO_session *fastIO_vsession_create(struct fastIO_state *cookie);


/*
 * Delete a fastIO Session
 */
  void fastIO_session_destroy(struct fastIO_session *sessioncookie);

/*
 * Allocate blocks
 */
  int fastIO_balloc(struct fastIO_state *cookie, int blockCount, struct fastIO_block **blocks, int flags);

  int fastIO_add_pkt(struct fastIO_state *cookie, struct fastIO_pkt **fioPkt,
                     int *fioPktCount, const char *pkt, int pktSize, uint16_t delaydelta);

/*
 * Add a split rule
 */
  void fastIO_add_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule);

/*
 * Remove a split rule
 */
  void fastIO_delete_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule);

/*
 * Delete the redirections for the split rule's srcIP and srcPort.
 */
  void fastIO_flush_split_rules(struct fastIO_session *srcSession, struct fastIO_split_rule *rule);


/*
 * Send UDP data
 */
  int fastIO_sendto(struct fastIO_session *cookie, uint32_t requestBlock);


/*
 * Setup a request block to be sent as part of a metarequest
 */
  int fastIO_metarequest_setup(struct fastIO_session *fio, uint32_t requestBlock);

/*
 * Send a metarequest
 */
  int fastIO_metarequest_send(struct fastIO_state *cookie, uint32_t requestBlock);
/*
 * Cleanup
 */
  void fastIO_fini(struct fastIO_state *cookie);

/*
 * Gather and display statistics
 *
 */
  void fastIO_print_stats(struct fastIO_state *cookie);

  void fastIO_get_bytes_stats(struct fastIO_session *srcSession, uint32_t * nbytesSent);
/*
 * Gather statistics
 *
 */
  void fastIO_get_stats(struct fastIO_state *cookie, struct ink_fio_stats *stats);

#else  // the else define is for non-sunos

/*
 * Initialize the fastIO system
  */
  static struct fastIO_state *fastIO_init(int blockcount)
  {
    return NULL;
  }


/*
 * Create a fastIO UDP session
 */
  static struct fastIO_session *fastIO_udpsession_create(struct fastIO_state *cookie, int fd)
  {
    return NULL;
  }

/*
 * Create a fastIO virtual session
 */
  static struct fastIO_session *fastIO_vsession_create(struct fastIO_state *cookie)
  {
    return NULL;
  }


/*
 * Delete a fastIO Session
 */
  static void fastIO_session_destroy(struct fastIO_session *sessioncookie)
  {
    return;
  }

/*
 * Allocate blocks
 */
  static int fastIO_balloc(struct fastIO_state *cookie, int blockCount, struct fastIO_block **blocks, int flags)
  {
    return 0;
  }

  static int fastIO_add_pkt(struct fastIO_state *cookie, struct fastIO_pkt **fioPkt,
                            int *fioPktCount, const char *pkt, int pktSize, uint16_t delaydelta)
  {
    return 0;
  }

/*
 * Add a split rule
 */
  static void fastIO_add_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
  {
    return;
  }

/*
 * Remove a split rule
 */
  static void fastIO_delete_split_rule(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
  {
    return;
  }

/*
 * Delete the redirections for the split rule's srcIP and srcPort.
 */
  static void fastIO_flush_split_rules(struct fastIO_session *srcSession, struct fastIO_split_rule *rule)
  {
    return;
  }


/*
 * Send UDP data
 */
  static int fastIO_sendto(struct fastIO_session *cookie, uint32_t requestBlock)
  {
    return 0;
  }


/*
 * Setup a request block to be sent as part of a metarequest
 */
  static int fastIO_metarequest_setup(struct fastIO_session *fio, uint32_t requestBlock)
  {
    return 0;
  }

/*
 * Send a metarequest
 */
  static int fastIO_metarequest_send(struct fastIO_state *cookie, uint32_t requestBlock)
  {
    return 0;
  }

/*
 * Cleanup
 */
  static void fastIO_fini(struct fastIO_state *cookie)
  {
    return;
  }

/*
 * Gather and display statistics
 *
 */
  static void fastIO_print_stats(struct fastIO_state *cookie)
  {
    return;
  }

  static void fastIO_get_bytes_stats(struct fastIO_session *srcSession, uint32_t * nbytesSent)
  {
    return;
  }
/*
 * Gather statistics
 *
 */
  static void fastIO_get_stats(struct fastIO_state *cookie, struct ink_fio_stats *stats)
  {
    return;
  }

#endif

#endif
