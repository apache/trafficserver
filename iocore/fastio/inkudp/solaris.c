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

char _depends_on[] = "drv/ip";
#define	MTYPE(m)	((m)->b_datap->db_type)

extern struct mod_ops mod_strmodops;    /* Kernel internal struct */

static int inkudp_ropen(queue_t *, dev_t *, int, int, cred_t *);
static int inkudp_rclose(queue_t *, dev_t *, int, int, cred_t *);
static int inkudp_rput(queue_t *, mblk_t *);
static int inkudp_wopen(queue_t *, dev_t *, int, int, cred_t *);
static int inkudp_wclose(queue_t *, dev_t *, int, int, cred_t *);
static int inkudp_wput(queue_t *, mblk_t *);
static int inkudp_srv(queue_t *);

/* Inkudp global data */
int *bufbaseptr;
int active = 0;
int blkcount;
int blocksize;                  /* FIXME: pass this through shared memory */
uint16_t *flist0, *flist1, *activefl;
int blockbaseptr;
int nextflentry;
int msgcount = 0;
int modopen = 0;

int redirect_enabled = 0;
int redirect_passthrough = 0;
struct ink_redirect_list *G_redirect_incoming_list;
struct ink_redirect_list *G_redirect_outgoing_list;
kmutex_t freemx;
kmutex_t G_incoming_splitmx;
kmutex_t G_outgoing_splitmx;

int qid;

queue_t *udp_queue;

struct free_rtn *free_struct;

/*
 *
 * Stupid GCC tries to use memcpy when it doesn't exist in the kernel library
 *  So we have to implement it
 *
 */
void *
memcpy(void *s1, void *s2, size_t n)
{
  bcopy(s2, s1, n);
  return s1;
}


/*
 * Notes:
 *       - Do I need to screw with the packet size, watermarks?
 *       - where can I get a unique id number
 */
static struct module_info minfo = {
  0xbabf,                       /* mi_idnum   module ID number */
  "inkudp",                     /* mi_idname  module name */
  0,                            /* mi_minpsz  minimum packet size */
  INFPSZ,                       /* mi_maxpsz  maximum packet size */
  518,                          /* mi_hiwat   high water mark */
  128                           /* mi_lowat   low water mark */
};


/*
 * Notes:
 *       - how does the stat structure work
 *       - will we need a service procedure for flow control
 */
static struct qinit rinit = {
  inkudp_rput,                  /* qi_put     put procedure */
  (int (*)()) NULL,             /* qi_srvp    service procedure */
  inkudp_ropen,                 /* qi_qopen   open procedure */
  inkudp_rclose,                /* qi_qclose  close procedure */
  (int (*)()) NULL,             /* qi_qadmin  _unused_ */
  &minfo,                       /* qi_minfo   module params */
  NULL                          /* qi_mstat   module stats */
};


static struct qinit winit = {
  inkudp_wput,                  /* qi_put     put procedure */
  (int (*)()) NULL,             /* qi_srvp    service procedure */
  inkudp_wopen,                 /* qi_qopen   open procedure */
  inkudp_wclose,                /* qi_qclose  close procedure */
  (int (*)()) NULL,             /* qi_qadmin  _unused_ */
  &minfo,                       /* qi_minfo   module params */
  NULL                          /* qi_mstat   module stats */
};


struct streamtab inkudp_info = {
  &rinit,                       /* st_rdinit    read queue */
  &winit,                       /* st_wrinit    write queue */
  NULL,                         /* st_muxrinit  lower read queue */
  NULL                          /* st_muxwinit  lower write queue */
};


static struct fmodsw inkudp_fsw = {
  "inkudp",                     /* f_name  module name */
  &inkudp_info,                 /* f_str   ptr to module streamtab */
  D_MTSAFE                      /* f_flag  module flags */
};


static struct modlstrmod inkudp_mod = {
  &mod_strmodops,               /* stdmod_modops    ptr to kernel mod_strmodops */
  "Inktomi UDP Accelerator v1.0",       /* strmod_linkinfo  module description */
  &inkudp_fsw                   /* strmod_fmodsw    ptr to module entry template */
};


static struct modlinkage inkudp_mod_link = {
  MODREV_1,                     /* ml_rev      driver revision */
  {
   &inkudp_mod,                 /* ml_linkage  ptr to module modldrv linkage */
   NULL}
};



void *
memset(void *s, int c, size_t n)
{
  bzero(s, n);
  return s;
}


int
_init()
{
  cmn_err(CE_CONT, "inkudp: _init\n");
  return mod_install(&inkudp_mod_link);
}


/*
 */
int
_fini()
{
  cmn_err(CE_CONT, "inkudp: _fini\n");
  return mod_remove(&inkudp_mod_link);
}


/*
 */
int
_info(struct modinfo *modinfop)
{
  return mod_info(&inkudp_mod_link, modinfop);
}




void
inkudp_dump_mblk(mblk_t * mp)
{

  mblk_t *trav;
  unsigned char *p;

  trav = mp;



  while (trav) {
    cmn_err(CE_CONT,
            "mblk<0x%x>: b_next<0x%x> b_prev<0x%x> b_cont<0x%x> pri<0x%x> flags<0x%x> rptr<0x%x> wptr<0x%x> size<%d>\n",
            trav,
            trav->b_next,
            trav->b_prev,
            trav->b_cont, trav->b_band, trav->b_flag, trav->b_rptr, trav->b_wptr, trav->b_wptr - trav->b_rptr);

    switch (trav->b_datap->db_type) {
    case M_BREAK:
      cmn_err(CE_CONT, "M_BREAK: ");
      break;
    case M_CTL:
      cmn_err(CE_CONT, "M_CTL: ");
      break;
    case M_DATA:
      cmn_err(CE_CONT, "M_DATA: ");
      break;
    case M_DELAY:
      cmn_err(CE_CONT, "M_DELAY: ");
      break;
    case M_IOCTL:
      cmn_err(CE_CONT, "M_IOCTL: ");
      break;
    case M_PASSFP:
      cmn_err(CE_CONT, "M_PASSFP: ");
      break;
    case M_PROTO:
      cmn_err(CE_CONT, "M_PROTO: ");
      break;
    case M_SETOPTS:
      cmn_err(CE_CONT, "M_SETOPTS: ");
      break;
    case M_SIG:
      cmn_err(CE_CONT, "M_SIG: ");
      break;
    case M_COPYIN:
      cmn_err(CE_CONT, "M_COPYIN: ");
      break;
    case M_COPYOUT:
      cmn_err(CE_CONT, "M_COPYOUT: ");
      break;
    case M_ERROR:
      cmn_err(CE_CONT, "M_ERROR: ");
      break;
    case M_FLUSH:
      cmn_err(CE_CONT, "M_FLUSH: ");
      break;
    case M_HANGUP:
      cmn_err(CE_CONT, "M_HANGUP: ");
      break;
    case M_UNHANGUP:
      cmn_err(CE_CONT, "M_UNHANGUP: ");
      break;
    case M_IOCACK:
      cmn_err(CE_CONT, "M_IOCACK: ");
      break;
    case M_IOCDATA:
      cmn_err(CE_CONT, "M_IOCDATA: ");
      break;
    case M_PCPROTO:
      cmn_err(CE_CONT, "M_PCPROTO: ");
      break;
    case M_PCSIG:
      cmn_err(CE_CONT, "M_PCSIG: ");
      break;
    case M_READ:
      cmn_err(CE_CONT, "M_READ: ");
      break;
    case M_START:
      cmn_err(CE_CONT, "M_START: ");
      break;
    case M_STARTI:
      cmn_err(CE_CONT, "M_STARTI: ");
      break;
    case M_STOP:
      cmn_err(CE_CONT, "M_STOP: ");
      break;
    case M_STOPI:
      cmn_err(CE_CONT, "M_STOPI: ");
      break;

    default:
      cmn_err(CE_CONT, "Unknown type:");
    }



    cmn_err(CE_CONT, "db_base<0x%x>, db_lim<0x%x>, db_ref<%d>, db_type<0x%x> size<%d>\n",
            trav->b_datap->db_base,
            trav->b_datap->db_lim,
            trav->b_datap->db_ref, trav->b_datap->db_type, trav->b_datap->db_lim - trav->b_datap->db_base);

    cmn_err(CE_CONT, "\nBuffer: ");


    if ((trav->b_wptr - trav->b_rptr) < 100)
      for (p = trav->b_rptr; p < trav->b_wptr; p++) {
        cmn_err(CE_CONT, " 0x%x/%d/'%c' ", *p, *p, *p);

    } else
      cmn_err(CE_CONT, "**skipping data, too much **\n");
    cmn_err(CE_CONT, "\n");
    if (trav->b_cont != trav && trav->b_cont != mp)
      trav = trav->b_cont;
    else
      trav = 0;
  }


}





/*
 */
static int
inkudp_ropen(queue_t * q, dev_t * devp, int flag, int sflag, cred_t * credp)
{
/*    cmn_err(CE_CONT, "inkudp_ropen\n");*/
  qprocson(q);

  return 0;
}


/*
 */
static int
inkudp_rclose(queue_t * q, dev_t * devp, int flag, int sflag, cred_t * credp)
{

  /*
     cmn_err(CE_CONT, "inkudp_rclose(q:0x%x, udp_queue:0x%x, qid:%d)\n",q, udp_queue); */
  modopen = 0;


  fio_emergency_unregister_queue(q);

  /* flush any redirect rules */
  /* inkudp_flush_split_rule(); */


  qprocsoff(q);

  return 0;
}


/*
 */
static int
inkudp_rput(queue_t * q, mblk_t * mp)
{


  /*cmn_err(CE_CONT, "inkudp_rput\n"); */

  if (mp->b_datap->db_type == M_PROTO) {
    inkudp_recv(mp, q);
  } else {

    if (!canputnext(q))
      cmn_err(CE_WARN, "inkudp_rput: unable to putnext\n");
    else
      putnext(q, mp);
  }

  return 0;
}


/*
 */
static int
inkudp_wopen(queue_t * q, dev_t * devp, int flag, int sflag, cred_t * credp)
{


/*    cmn_err(CE_CONT,"inkudp_wopen (udpqueue:0x%x) \n",q);*/

  qprocson(q);



  return 0;
}


/*
 */
static int
inkudp_wclose(queue_t * q, dev_t * devp, int flag, int sflag, cred_t * credp)
{


  /*
   *  For whatever bizarre reason, this side of the queue never
   *  actually gets opened.
   *
   */

/*    cmn_err(CE_CONT,"inkudp_wclose\n");*/

  modopen = 0;


  /* inkudp_flush_split_rule(); */

  /* unregister the queue */
  /*fio_unregister_queue(qid); */

  qprocsoff(q);

  return 0;
}


/*
 * callback for freeing of message blocks
 */
void
inkudp_free_cb(char *dat)
{
  uint16_t id = (uint16_t) ((int) dat);

  if (!modopen) {
    /*cmn_err(CE_CONT, "inkudp_free_cb: Called after shutdown (%d).\n",id); */
    return;
  }

/*    cmn_err(CE_CONT, "inkudp_free_cb: #%d @%d active: %d", id, nextflentry, active);*/




  mutex_enter(&freemx);
  activefl[nextflentry] = id;
  nextflentry++;
  mutex_exit(&freemx);

}

/*
 * Return a pointer to the requested block
 */
inline void *
getBlockPtr(uint16_t id)
{
  int dist = id * blocksize;
  dist += blockbaseptr;

  return (void *) dist;
}

/*
 * Initialize a STREAMS UDP request message body
 */
inline void
inkudp_udppkt_init(struct udppkt *p)
{
  /* the magic numbers get hardcoded
   *
   * Fixme: are these platform-dependant?
   * How will they work on sparc?
   */
#ifdef i386
  char data[22] = { 0x08, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x02, 0x00
  };
  char ftr[8] = { 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
#endif
#ifdef sparc

  char data[22] = { 0x00, 0x00, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x14,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x02
  };
  char ftr[8] = { 0x00, 0x00, 0x00, 0x41,
    0x00, 0x00, 0x00, 0x00
  };
#endif

  bcopy(data, &p->hdr, 22);
  bcopy(ftr, &p->ftr, 8);
}

/*
 *  Construct a message with the proper destination block
 */
inline mblk_t *
inkudp_dstmsg_create(int32_t ip, int16_t port)
{

  mblk_t *mp;
  struct udppkt *buf;



/*    cmn_err(CE_NOTE, "inkudp: udppkt is %d bytes.\n", sizeof(struct udppkt));*/
  mp = (mblk_t *) allocb(72, 0);

  /* cmn_err(CE_NOTE, "* * * * * *  NEWLY ALLOCATED DMSG * * * * *");
     inkudp_dump_mblk(mp);  */

  if (!mp) {
    cmn_err(CE_WARN, "inkudp: out of memory!\n");
    return 0;
  }


  buf = (struct udppkt *) mp->b_wptr;

  inkudp_udppkt_init(buf);

  buf->port = port;
  buf->ip = ip;



  /* set the message type to M_PROTO */
  mp->b_datap->db_type = M_PROTO;
  mp->b_wptr = mp->b_rptr + sizeof(struct udppkt);
  return mp;
}


/*
 * Handle IOCTLs that are for us
 *
 * Return 1 if we were able to handle it.  0 otherwise
 *
 * mp pust be a poiinter to a mblk_t of type M_IOCTL
 */
static int
inkudp_handle_ioctl(mblk_t * mp, queue_t * q)
{
  struct strioctl *ioctlp;
  struct iocblk *iocp;




  int bufptr;
  ioctlp = (struct strioctl *) mp->b_rptr;
  iocp = (struct iocblk *) mp->b_rptr;

  /*
     cmn_err(CE_CONT, "ioctl cmd: %d, timeout: %d, len: %d, data: 0x%x\n",
     ioctlp->ic_cmd, ioctlp->ic_timout, ioctlp->ic_len, ioctlp->ic_dp); */


  /* inkudp_dump_mblk(mp); */

  if (ioctlp->ic_cmd & INK_CMD_SPLIT_IOCTLMASK) {

    inkudp_handle_cmsg(mp->b_cont, OTHERQ(q));

    mp->b_datap->db_type = M_IOCACK;
    iocp->ioc_rval = (int) 0;
    iocp->ioc_count = 0;
    qreply(q, mp);
    return 1;
  }

  switch (ioctlp->ic_cmd) {
  case INKUDP_INIT:
    /* NO longer supported */
    break;
    /*return inkudp_ioctl_init(mp,q); */

  case INKUDP_SENDTO:
    /* NO longer supported */
    break;

  case INKUDP_SWAP:
    /* No Longer supported */
    break;
  case INKUDP_FINI:
    return inkudp_ioctl_fini(mp, q);
  case INKUDP_GETQ:

    qid = fio_register_queue(q);
    /* cmn_err(CE_NOTE, "Registering queue: 0x%x\n", q); */

    mp->b_datap->db_type = M_IOCACK;
    iocp->ioc_rval = (int) qid;
    iocp->ioc_count = 0;
    qreply(q, mp);
    return 1;


  }
  return 0;

}


/*
 */
static int
inkudp_wput(queue_t * q, mblk_t * mp)
{




/*    inkudp_dump_mblk(mp);*/
  udp_queue = q;

  msgcount++;
  if (mp->b_datap->db_type == M_IOCTL && inkudp_handle_ioctl(mp, q)) {
    /*nuttin */
  } else
    putnext(q, mp);





  return 0;



}




/*
 * Service proceedure
 *
 */
static int
inkudp_srv(queue_t * q)
{
  mblk_t *mp;

/*    cmn_err(CE_CONT, "inkudp_srv.\n");*/
  while ((mp = getq(q)) != NULL) {

    putnext(q, mp);
  }


}
