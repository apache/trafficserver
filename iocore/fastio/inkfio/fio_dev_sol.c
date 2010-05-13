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
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include "fio_dev.h"

#include "fastio.h"

/*
 * An opaque handle where our set of fio devices lives
 */
void *fio_state;

/* global instance pointer */
fio_devstate_t *g_fio_instance;

static int fio_open(dev_t * devp, int flag, int otyp, cred_t * cred);
static int fio_close(dev_t dev, int flag, int otyp, cred_t * cred);
static int fio_read(dev_t dev, struct uio *uiop, cred_t * credp);
static int fio_write(dev_t dev, struct uio *uiop, cred_t * credp);
static int fio_print(dev_t dev, char *str);
static int fio_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len, size_t * maplen, uint_t model);

static void fio_unmap(devmap_cookie_t dhp, void *pvtp, offset_t off,
                      size_t len, devmap_cookie_t new_dhp1, void **new_pvtp1,
                      devmap_cookie_t new_dhp2, void **new_pvtp2);


int fio_register_queue(queue_t * q);


static struct cb_ops fio_cb_ops = {
  fio_open,
  fio_close,                    /* close */
  nodev,                        /* fio_strategy, */
  fio_print,
  nodev,                        /* dump */
  fio_read,
  fio_write,
  fio_ioctl,                    /* ioctl */
  fio_devmap,                   /* devmap */
  nodev,                        /* mmap */
  nodev,                        /* segmap */
  nochpoll,                     /* poll */
  ddi_prop_op,
  NULL,
  D_NEW | D_MP | D_DEVMAP
};

static int fio_getinfo(dev_info_t * dip, ddi_info_cmd_t infocmd, void *arg, void **result);
static int fio_attach(dev_info_t * dip, ddi_attach_cmd_t cmd);
static int fio_detach(dev_info_t * dip, ddi_detach_cmd_t cmd);

static struct dev_ops fio_ops = {
  DEVO_REV,
  0,
  fio_getinfo,
  nulldev,                      /* identify */
  nulldev,                      /* probe */
  fio_attach,
  fio_detach,
  nodev,                        /* reset */
  &fio_cb_ops,
  (struct bus_ops *) 0
};


extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
  &mod_driverops,
  "Inktomi FastIO Device v1.0",
  &fio_ops
};

static struct modlinkage modlinkage = {
  MODREV_1,
  &modldrv,
  0
};


static struct devmap_callback_ctl mmap_callback = {
  DEVMAP_OPS_REV,
  NULL,                         /* devmap_map */
  NULL,                         /* devmap_access */
  NULL,                         /* devmap_dup */
  fio_unmap                     /*devmap_unmap */
};

int
_init(void)
{
  int e;
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: _init\n");
#endif

  g_fio_instance = 0;

  if ((e = ddi_soft_state_init(&fio_state, sizeof(fio_devstate_t), 1)) != 0) {

    cmn_err(CE_CONT, "fio: soft_state init fails.\n");
    return (e);
  }

  if ((e = mod_install(&modlinkage)) != 0) {
    cmn_err(CE_CONT, "fio: mod_install failed.");
    ddi_soft_state_fini(&fio_state);
  }

  cmn_err(CE_CONT, "fio: init returns %d.\n", e);
  return (e);
}


int
_fini(void)
{
  int e;
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: _fini\n");
#endif


  if ((e = mod_remove(&modlinkage)) != 0) {
    return (e);
  }
  cmn_err(CE_CONT, "fio: finishing...\n");
  ddi_soft_state_fini(&fio_state);
  return (e);
}

int
_info(struct modinfo *modinfop)
{
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: modinfo\n");
#endif
  return (mod_info(&modlinkage, modinfop));
}

static int
fio_attach(dev_info_t * dip, ddi_attach_cmd_t cmd)
{
  int nblocks;
  int instance, i;
  fio_devstate_t *rsp;

  cmn_err(CE_CONT, "fio: _attach\n");


  switch (cmd) {

  case DDI_ATTACH:

    instance = ddi_get_instance(dip);

    if (ddi_soft_state_zalloc(fio_state, instance) != DDI_SUCCESS) {
      cmn_err(CE_CONT, "%s%d: can't allocate state\n", ddi_get_name(dip), instance);
      return (DDI_FAILURE);
    }

    rsp = ddi_get_soft_state(fio_state, instance);
    /* store the soft state in a global pointer */
    g_fio_instance = rsp;

    /* initialize session stuff */
    rsp->session_count = 0;
    bzero(&rsp->session, sizeof(queue_t *) * MAX_SESSION);
    for (i = 0; i < MAX_SESSION; i++)
      mutex_init(&rsp->session_mutex[i], NULL, MUTEX_DRIVER, NULL);

    rsp->dip = dip;
    rsp->ram = 0;
    if ((ddi_create_minor_node(dip, "x", S_IFCHR, instance, DDI_PSEUDO, 0) == DDI_FAILURE)) {
      cmn_err(CE_CONT, "fio: Unable to create minor device\n");
      ddi_remove_minor_node(dip, NULL);
      goto attach_failed;
    }


    cmn_err(CE_CONT, "fio: attach success.\n");
    return (DDI_SUCCESS);

  default:
    return (DDI_FAILURE);
  }

attach_failed:
  /*
   * Use our own detach routine to toss
   * away any stuff we allocated above.
   */
  (void) fio_detach(dip, DDI_DETACH);
  return (DDI_FAILURE);
}

static int
fio_detach(dev_info_t * dip, ddi_detach_cmd_t cmd)
{
  int instance, i;
  register fio_devstate_t *rsp;
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: detach\n");
#endif

  switch (cmd) {

  case DDI_DETACH:
    /*
     * Undo what we did in fio_attach, freeing resources
     * and removing things we installed.  The system
     * framework guarantees we are not active with this devinfo
     * node in any other entry points at this time.
     */
    ddi_prop_remove_all(dip);
    instance = ddi_get_instance(dip);
    rsp = ddi_get_soft_state(fio_state, instance);
    /* free the queue mutexes */
    for (i = 0; i < MAX_SESSION; i++)
      mutex_destroy(&g_fio_instance->session_mutex[i]);

    /* remove the global instance */
    g_fio_instance = 0;

    if (rsp->timeout_id) {
      cmn_err(CE_NOTE, "fio: Cancelling callback.\n");
      untimeout(rsp->timeout_id);
    }

    ddi_remove_minor_node(dip, NULL);
    ddi_soft_state_free(fio_state, instance);
    return (DDI_SUCCESS);

  default:
    return (DDI_FAILURE);
  }
}

 /*ARGSUSED*/ static int
fio_getinfo(dev_info_t * dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
  fio_devstate_t *rsp;
  int error = DDI_FAILURE;
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: getinfo\n");
#endif

  switch (infocmd) {
  case DDI_INFO_DEVT2DEVINFO:
    if ((rsp = ddi_get_soft_state(fio_state, getminor((dev_t) arg))) != NULL) {
      *result = rsp->dip;
      error = DDI_SUCCESS;
    } else
      *result = NULL;
    break;

  case DDI_INFO_DEVT2INSTANCE:
    *result = (void *) getminor((dev_t) arg);
    error = DDI_SUCCESS;
    break;

  default:
    break;
  }

  return (error);
}


 /*ARGSUSED*/ static int
fio_open(dev_t * devp, int flag, int otyp, cred_t * cred)
{

  fio_devstate_t *rsp;
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: open\n");
#endif


  if (otyp != OTYP_BLK && otyp != OTYP_CHR)
    return (EINVAL);

  if ((rsp = ddi_get_soft_state(fio_state, getminor(*devp))) == NULL)
    return (ENXIO);
  if (rsp->modopen) {
    cmn_err(CE_CONT, "fio_open: Error device already open!.\n");
    return ENXIO;
  }

  return (0);
}

 /*ARGSUSED*/ static int
fio_close(dev_t dev, int flag, int otyp, cred_t * cred)
{
  fio_devstate_t *rsp;
  int i;

  cmn_err(CE_CONT, "fio: close\n");


  if (otyp != OTYP_BLK && otyp != OTYP_CHR)
    return (EINVAL);


  rsp = ddi_get_soft_state(fio_state, getminor(dev));
  if (!rsp) {
    cmn_err(CE_CONT, "fio: close, unable to get soft state\n");
    return (ENXIO);

  }
  if (rsp->timeout_id)
    untimeout(rsp->timeout_id);
  rsp->timeout_id = 0;


  /* If the naughty user left virtual sessions open,
   * clean up for them
   */
  mutex_enter(&rsp->reqmx);
  mutex_enter(&rsp->modopenmx);
  rsp->modopen = 0;
  mutex_exit(&rsp->modopenmx);
  mutex_exit(&rsp->reqmx);


  /*
     cmn_err(CE_CONT, "fio: destroying vsessions...\n");
   */
  for (i = 0; i < MAX_VSESSION; i++) {
    if (rsp->vsession_alloc[i]) {
      fio_vsession_destroy(rsp, i);

    }
  }

  /* destroy any registered queues */

  for (i = 0; i < MAX_SESSION; i++) {

    if (!mutex_owned(&rsp->session_mutex[i]))
      mutex_enter(&rsp->session_mutex[i]);

    if (rsp->session[i])
      rsp->session[i] = 0;

    mutex_exit(&rsp->session_mutex[i]);
  }
  rsp->session_count = 0;

  /*
     cmn_err(CE_CONT, "fio: done destroyign vsessions.\n");
   */


  if (rsp->ram)
    ddi_umem_free(rsp->cookie);
  rsp->ram = 0;


  cmn_err(CE_CONT, "fio: Close: success\n");
  return (0);
}

 /*ARGSUSED*/ static int
fio_read(dev_t dev, struct uio *uiop, cred_t * credp)
{
  int instance = getminor(dev);

#ifdef DEBUG
  cmn_err(CE_CONT, "fio: readn");
#endif

  return DDI_FAILURE;



}

 /*ARGSUSED*/ static int
fio_write(dev_t dev, register struct uio *uiop, cred_t * credp)
{
  int instance = getminor(dev);

#ifdef DEBUG
  cmn_err(CE_CONT, "fio: write");
#endif

  return DDI_FAILURE;
}

static int
fio_print(dev_t dev, char *str)
{
  int instance = getminor(dev);
  fio_devstate_t *rsp = ddi_get_soft_state(fio_state, instance);

  cmn_err(CE_WARN, "%s%d: %s\n", ddi_get_name(rsp->dip), instance, str);
  return (0);
}

/*
 * MMAP handler
 *
 */
static int
fio_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len, size_t * maplen, uint_t model)
{


  fio_devstate_t *rsp;
  int error;


  /* round up to page size */
  len = ptob(btopr(len));

  rsp = ddi_get_soft_state(fio_state, getminor(dev));
  if (!rsp)
    return ENXIO;


  if (rsp->ram) {
    cmn_err(CE_WARN, "fio: Only one mapping allowed per device instance.\n");
    return ENXIO;

  }
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: attempting to allocate 0x%x bytes at offset 0x%x\n", len, off);
#endif
  rsp->ram = ddi_umem_alloc(len, DDI_UMEM_SLEEP, &rsp->cookie);
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: allocated %d bytes at 0x%x\n", len, rsp->ram);
#endif
  if (!rsp->ram)
    return ENXIO;

  /* touch ALL of it! */
/*    cmn_err(CE_CONT, "Touching all the memory...\n");*/
  /*bzero(rsp->ram, len); */

  error = devmap_umem_setup(dhp, rsp->dip, NULL, rsp->cookie, (offset_t) off, len, PROT_ALL, 0, NULL);
  if (error < 0) {
    cmn_err(CE_WARN, "fio: devmap_umem_setup fails.  Retval: %d\n", error);
    cmn_err(CE_WARN,
            "fio: dhp:0x%x, rsp->dip: 0x%x, rsp->cookie:0x%x, rsp->ram:0x%x, len:0x%x, PROT: 0x%x, DM_DEF: 0x%x\n", dhp,
            rsp->dip, rsp->cookie, (offset_t) off, len, PROT_ALL, DEVMAP_DEFAULTS);
    cmn_err(CE_WARN, "fio: len is %d (0x%x).\n.", len, len);

    return ENXIO;
  }
  *maplen = len;


  return 0;

}

/*
 *
 * MUNMAP handler
 */
static void
fio_unmap(devmap_cookie_t dhp, void *pvtp, offset_t off,
          size_t len, devmap_cookie_t new_dhp1, void **new_pvtp1, devmap_cookie_t new_dhp2, void **new_pvtp2)
{
#ifdef DEBUG
  cmn_err(CE_CONT, "fio: fio_unmap()\n");
#endif


  ddi_umem_free(dhp);

  cmn_err(CE_CONT, "fio: freed allocated memory.\n");
  return;

}


/*
 *  Called by STREAMS modules to register themselves
 *
 */
int
fio_register_queue(queue_t * q)
{

  int i;

  if (!g_fio_instance) {
    cmn_err(CE_WARN, "Attempt to register a queue while fastIO not open.\n");
    return -1;
  }

  if (g_fio_instance->session_count == MAX_SESSION) {
    cmn_err(CE_NOTE, "fio_register_queue: Queue limit reached. Potential queue leak.\n");
    return -1;
  }

  for (i = 0; i < MAX_SESSION; i++) {

    if (!g_fio_instance->session[i]) {
      g_fio_instance->session[i] = q;
      g_fio_instance->session_count++;

      /* update statistics */
      g_fio_instance->stats.sessions_open = g_fio_instance->session_count;

      return i;

    }
  }

  /* Should never be here:  session_count indicates that there should be free QID's,
   * but we didn't find one
   */

  cmn_err(CE_CONT, "inkfio: Out of Hunk registering queue 0x%x.\n", q);

  return 0;
}


/*
 * Called by INKUDP IOCTL_FINI handler to un-register itself
 *
 */
void
fio_unregister_queue(int qid)
{
  /* doesn't matter if the module is closed */
  if (!g_fio_instance)
    return;


  if (qid >= MAX_SESSION)
    return;                     /* someone's on crack... */

  /* already free? */
  if (!g_fio_instance->session[qid])
    return;

  if (!fio_acquire_queue(qid, g_fio_instance->session[qid]))
    return;

  /* goofy cases notwithstanding, now we actually mark the queue ID free */
  g_fio_instance->session[qid] = 0;

  g_fio_instance->session_count--;

  /* update statistics */
  g_fio_instance->stats.sessions_open = g_fio_instance->session_count;

  fio_release_queue(qid);

  return;
}


/*
 * Called by INKUDP rclose handler to un-register itself
 *
 */
void
fio_emergency_unregister_queue(queue_t * q)
{
  int i, release_reqmx = 0;
  struct pending_request *trav, *nextreq;
  queue_t *removalQ;

  /* doesn't matter if the module is closed */
  if (!g_fio_instance)
    return;

  for (i = 0; i < MAX_SESSION; i++) {

    /* Found the one we are looking for */
    if ((g_fio_instance->session[i] == q) || (g_fio_instance->session[i] == OTHERQ(q))) {
      if (!fio_acquire_queue(i, q))
        return;

      if (g_fio_instance->session[i] == q)
        removalQ = q;
      else
        removalQ = OTHERQ(q);

      g_fio_instance->session[i] = 0;
      g_fio_instance->session_count--;

      /* update statistics */
      g_fio_instance->stats.sessions_open = g_fio_instance->session_count;

      /* release the queue asap */
      fio_release_queue(i);

      return;
      return;
    }
  }
}


queue_t *
fio_lookup_queue(int qid)
{
/* error if the module is closed */
  if (!g_fio_instance)
    return NULL;

  if (qid >= MAX_SESSION)
    return NULL;                /* someone's on crack... */


  if (!g_fio_instance->session[qid])
    return NULL;                /*already free */

  return g_fio_instance->session[qid];
}

int
fio_acquire_queue(int qid, queue_t * q)
{
  if (!g_fio_instance)
    return 0;

  if (!mutex_owned(&g_fio_instance->session_mutex[qid])) {
    mutex_enter(&g_fio_instance->session_mutex[qid]);
  }
  /* Take the lock and verify that the queue pointer is still valid */
  if ((g_fio_instance->session[qid] == q) || (g_fio_instance->session[qid] == OTHERQ(q)))
    return 1;
  else
    fio_release_queue(qid);

  return 0;
}

void
fio_release_queue(int qid)
{
  if (!g_fio_instance)
    return;

  if (mutex_owned(&g_fio_instance->session_mutex[qid]))
    mutex_exit(&g_fio_instance->session_mutex[qid]);
}
