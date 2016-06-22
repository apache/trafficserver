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
 *  WebOverview.cc - code to overview page
 *
 *
 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_string.h"
#include "MgmtDefs.h"
#include "WebOverview.h"
#include "WebMgmtUtils.h"

#include "LocalManager.h"
#include "ClusterCom.h"
#include "MgmtUtils.h"
#include "MgmtDefs.h"
#include "ts/Diags.h"

// Make this pointer to avoid nasty destruction
//   problems do to alarm
//   fork, execl, exit squences
overviewPage *overviewGenerator;

overviewRecord::overviewRecord(unsigned long inet_addr, bool local, ClusterPeerInfo *cpi)
{
  char *name_l; // hostname looked up from node record
  bool name_found;
  struct in_addr nameFailed;

  inetAddr = inet_addr;

  this->up        = false;
  this->localNode = local;

  // If this is the local node, there is no cluster peer info
  //   record.  Remote nodes require a cluster peer info record
  ink_assert((local == false && cpi != NULL) || (local == true && cpi == NULL));

  // Set up the copy of the records array and initialize it
  if (local == true) {
    node_rec_data.num_recs = 0;
    node_rec_data.recs     = NULL;
    recordArraySize        = 0;
    node_rec_first_ix      = 0;
  } else {
    node_rec_data.num_recs = cpi->node_rec_data.num_recs;
    recordArraySize        = node_rec_data.num_recs * sizeof(RecRecord);
    node_rec_data.recs     = new RecRecord[recordArraySize];
    memcpy(node_rec_data.recs, cpi->node_rec_data.recs, recordArraySize);

    // Recaculate the old relative index
    RecGetRecordOrderAndId(node_rec_data.recs[0].name, &node_rec_first_ix, NULL);
  }

  // Query for the name of the node.  If it is not there, some
  //   their cluster ip address
  name_l = this->readString("proxy.node.hostname_FQ", &name_found);
  if (name_found == false || name_l == NULL) {
    nameFailed.s_addr = inetAddr;
    mgmt_log("[overviewRecord::overviewRecord] Unable to find hostname for %s\n", inet_ntoa(nameFailed));
    ats_free(name_l); // about to overwrite name_l, so we need to free it first
    name_l = ats_strdup(inet_ntoa(nameFailed));
  }

  const size_t hostNameLen = strlen(name_l) + 1;
  this->hostname           = new char[hostNameLen];
  ink_strlcpy(this->hostname, name_l, hostNameLen);
  ats_free(name_l);
}

overviewRecord::~overviewRecord()
{
  delete[] hostname;

  if (localNode == false) {
    delete[] node_rec_data.recs;
  }
}

// void overviewRecord::updateStatus(time_t, ClusterPeerInfo*)
// updates up/down status based on the cluster peer info record
//
//   currentTime is the value of localtime(time()) - sent in as
//     a parameter so we do not have to make repetitive system calls.
//     overviewPage::checkForUpdates can just make one call
//
//   cpi - is a pointer to a structure we got from ClusterCom that represnets
//         information about this node
//
//   a machine is up if we have heard from it in the last 15 seconds
//
void
overviewRecord::updateStatus(time_t currentTime, ClusterPeerInfo *cpi)
{
  // Update if the node is up or down
  if (currentTime - cpi->idle_ticks > 15) {
    up = false;
  } else {
    up = true;
  }

  // Update the node records by copying them from cpi
  //  (remote nodes only)
  if (localNode == false) {
    memcpy(node_rec_data.recs, cpi->node_rec_data.recs, recordArraySize);
    RecGetRecordOrderAndId(node_rec_data.recs[0].name, &node_rec_first_ix, NULL);
  }
}

// overview::readInteger
//
//  Accessor functions for node records.  For remote node,
//    we get the value in the node_data array we maintain
//    in this object.  For the node, we do not maintain any data
//    and rely on lmgmt->record_data for both the retrieval
//    code and the records array
//
//  Locking should be done by overviewPage::accessLock.
//  CALLEE is responsible for obtaining and releasing the lock
//
RecInt
overviewRecord::readInteger(const char *name, bool *found)
{
  RecInt rec     = 0;
  int rec_status = REC_ERR_OKAY;
  int order      = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_assert(order < node_rec_data.num_recs);
      rec = node_rec_data.recs[order].data.rec_int;
    }
  } else {
    rec_status = RecGetRecordInt(name, &rec);
  }

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

RecFloat
overviewRecord::readFloat(const char *name, bool *found)
{
  RecFloat rec   = 0.0;
  int rec_status = REC_ERR_OKAY;
  int order      = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_assert(order < node_rec_data.num_recs);
      rec = node_rec_data.recs[order].data.rec_float;
    }
  } else {
    rec_status = RecGetRecordFloat(name, &rec);
  }

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

RecString
overviewRecord::readString(const char *name, bool *found)
{
  RecString rec  = NULL;
  int rec_status = REC_ERR_OKAY;
  int order      = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_assert(order < node_rec_data.num_recs);
      rec = ats_strdup(node_rec_data.recs[order].data.rec_string);
    }
  } else {
    rec_status = RecGetRecordString_Xmalloc(name, &rec);
  }

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

//  overview::readData, read RecData according varType
//
//  Accessor functions for node records.  For remote node,
//    we get the value in the node_data array we maintain
//    in this object.  For the node, we do not maintain any data
//    and rely on lmgmt->record_data for both the retrieval
//    code and the records array
//
//  Locking should be done by overviewPage::accessLock.
//  CALLEE is responsible for obtaining and releasing the lock
//
RecData
overviewRecord::readData(RecDataT varType, const char *name, bool *found)
{
  int rec_status = REC_ERR_OKAY;
  int order      = -1;
  RecData rec;
  RecDataZero(RECD_NULL, &rec);

  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_assert(order < node_rec_data.num_recs);
      RecDataSet(varType, &rec, &node_rec_data.recs[order].data);
    } else {
      Fatal("node variables '%s' not found!\n", name);
    }
  } else
    rec_status = RecGetRecord_Xmalloc(name, varType, &rec, true);

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

bool
overviewRecord::varFloatFromName(const char *name, MgmtFloat *value)
{
  bool found = false;

  if (value)
    *value = readFloat((char *)name, &found);

  return found;
}

overviewPage::overviewPage() : sortRecords(10, false)
{
  ink_mutex_init(&accessLock, "overviewRecord");
  nodeRecords = ink_hash_table_create(InkHashTableKeyType_Word);
  numHosts    = 0;
  ourAddr     = 0; // We will update this when we add the record for
  //  this machine
}

overviewPage::~overviewPage()
{
  // Since we only have one global object and we never destruct it
  //  do not actually free memeory since it causes problems the
  //  process is vforked, and the child execs something
  // The below code is DELIBERTLY commented out
  //
  // ink_mutex_destroy(&accessLock);
  // ink_hash_table_destroy(nodeRecords);
}

// overviewPage::checkForUpdates - updates node records as to whether peers
//    are up or down
void
overviewPage::checkForUpdates()
{
  ClusterPeerInfo *tmp;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  overviewRecord *current;
  time_t currentTime;
  bool newHostAdded = false;

  // grok through the cluster communication stuff and update information
  //  about hosts in the cluster
  //
  ink_mutex_acquire(&accessLock);
  ink_mutex_acquire(&(lmgmt->ccom->mutex));
  currentTime = time(NULL);
  for (entry = ink_hash_table_iterator_first(lmgmt->ccom->peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(lmgmt->ccom->peers, &iterator_state)) {
    tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(lmgmt->ccom->peers, entry);

    if (ink_hash_table_lookup(nodeRecords, (InkHashTableKey)tmp->inet_address, (InkHashTableValue *)&current) == 0) {
      this->addRecord(tmp);
      newHostAdded = true;
    } else {
      current->updateStatus(currentTime, tmp);
    }
  }
  ink_mutex_release(&lmgmt->ccom->mutex);

  // If we added a new host we must resort sortRecords
  if (newHostAdded) {
    this->sortHosts();
  }

  ink_mutex_release(&accessLock);
}

// overrviewPage::sortHosts()
//
// resorts sortRecords, but always leaves the local node
//   as the first record
//
// accessLock must be held by callee
void
overviewPage::sortHosts()
{
  void **array = sortRecords.getArray();

  qsort(array + 1, numHosts - 1, sizeof(void *), hostSortFunc);
}

// overviewPage::addRecord(ClusterPerrInfo* cpi)
//   Adds a new node record
//   Assuems that this->accessLock is already held
//
void
overviewPage::addRecord(ClusterPeerInfo *cpi)
{
  overviewRecord *newRec;

  ink_assert(cpi != NULL);

  newRec = new overviewRecord(cpi->inet_address, false, cpi);
  newRec->updateStatus(time(NULL), cpi);

  ink_hash_table_insert(nodeRecords, (InkHashTableKey)cpi->inet_address, (InkHashTableEntry *)newRec);

  sortRecords.addEntry(newRec);
  numHosts++;
}

// adds a record to nodeRecords for the local machine.
//   gets IP addr from lmgmt->ccom so cluster communtication
//   must be intialized before calling this function
//
//
void
overviewPage::addSelfRecord()
{
  overviewRecord *newRec;

  ink_mutex_acquire(&accessLock);

  // We should not have been called before
  ink_assert(ourAddr == 0);

  // Find out what our cluster addr is from
  //   from cluster com
  this->ourAddr = lmgmt->ccom->getIP();

  newRec     = new overviewRecord(ourAddr, true);
  newRec->up = true;

  ink_hash_table_insert(nodeRecords, (InkHashTableKey)this->ourAddr, (InkHashTableEntry *)newRec);

  sortRecords.addEntry(newRec);
  numHosts++;
  ink_mutex_release(&accessLock);
}

// overviewRecord* overviewPage::findNodeByName(const char* nodeName)
//
//   Returns a pointer to node name nodeName
//     If node name is not found, returns NULL
//
//   CALLEE MUST BE HOLDING this->accessLock
//
overviewRecord *
overviewPage::findNodeByName(const char *nodeName)
{
  overviewRecord *current = NULL;
  bool nodeFound          = false;

  // Do a linear search of the nodes for this nodeName.
  //   Yes, I know this is slow but the current word is ten
  //   nodes would be a huge cluster so this should not
  //   be a problem
  //
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *)sortRecords[i];
    if (strcmp(nodeName, current->hostname) == 0) {
      nodeFound = true;
      break;
    }
  }

  if (nodeFound == true) {
    return current;
  } else {
    return NULL;
  }
}

// MgmtString overviewPage::readString(const char* nodeName, char* *name, bool *found = NULL)
//
//   Looks up a node record for a specific by nodeName
//    CALLEE deallocates the string with free()
//
MgmtString
overviewPage::readString(const char *nodeName, const char *name, bool *found)
{
  MgmtString r = NULL;
  //  bool nodeFound = false;
  bool valueFound = false;
  overviewRecord *node;

  ink_mutex_acquire(&accessLock);

  node = this->findNodeByName(nodeName);

  if (node != NULL) {
    r = node->readString(name, &valueFound);
  }
  ink_mutex_release(&accessLock);

  if (found != NULL) {
    *found = valueFound;
  }

  return r;
}

// MgmtInt overviewPage::readInteger(const char* nodeName, char* *name, bool *found = NULL)
//
//   Looks up a node record for a specific by nodeName
//
MgmtInt
overviewPage::readInteger(const char *nodeName, const char *name, bool *found)
{
  MgmtInt r = -1;
  //  bool nodeFound = false;
  bool valueFound = false;
  overviewRecord *node;

  ink_mutex_acquire(&accessLock);

  node = this->findNodeByName(nodeName);

  if (node != NULL) {
    r = node->readInteger(name, &valueFound);
  }
  ink_mutex_release(&accessLock);

  if (found != NULL) {
    *found = valueFound;
  }

  return r;
}

// MgmtFloat overviewPage::readFloat(const char* nodeName, char* *name, bool *found = NULL)
//
//   Looks up a node record for a specific by nodeName
//
RecFloat
overviewPage::readFloat(const char *nodeName, const char *name, bool *found)
{
  RecFloat r = -1.0;
  //  bool nodeFound = false;
  bool valueFound = false;
  overviewRecord *node;

  ink_mutex_acquire(&accessLock);

  node = this->findNodeByName(nodeName);

  if (node != NULL) {
    r = node->readFloat(name, &valueFound);
  }
  ink_mutex_release(&accessLock);

  if (found != NULL) {
    *found = valueFound;
  }

  return r;
}

// int overviewPage::clusterSumData(RecDataT varType, const char* nodeVar,
//                                  RecData* sum)
//
//   Sums nodeVar for every up node in the cluster and stores the
//     sum in *sum.  Returns the number of nodes summed over
//
//   CALLEE MUST HOLD this->accessLock
//
int
overviewPage::clusterSumData(RecDataT varType, const char *nodeVar, RecData *sum)
{
  int numUsed        = 0;
  int numHosts_local = sortRecords.getNumEntries();
  overviewRecord *current;
  bool found;
  RecData recTmp;

  ink_assert(sum != NULL);
  RecDataZero(varType, sum);

  for (int i = 0; i < numHosts_local; i++) {
    current = (overviewRecord *)sortRecords[i];
    if (current->up == true) {
      numUsed++;
      recTmp = current->readData(varType, nodeVar, &found);
      *sum   = RecDataAdd(varType, *sum, recTmp);
      if (found == false) {
      }
    }
  }
  return numUsed;
}

int
overviewPage::varClusterDataFromName(RecDataT varType, const char *nodeVar, RecData *sum)
{
  int status = 0;

  ink_mutex_acquire(&accessLock);

  status = clusterSumData(varType, nodeVar, sum);

  ink_mutex_release(&accessLock);

  return (status);
}

// Moved from the now removed StatAggregation.cc
void
AgFloat_generic_scale_to_int(const char *processVar, const char *nodeVar, double factor)
{
  MgmtFloat tmp;

  if (varFloatFromName(processVar, &tmp)) {
    tmp = tmp * factor;
    tmp = tmp + 0.5; // round up.
    varSetInt(nodeVar, (int)tmp);
  } else {
    varSetInt(nodeVar, -20);
  }
}

// char* overviewPage::resolvePeerHostname(char* peerIP)
//
//   A locking interface to overviewPage::resolvePeerHostname_ml
//
char *
overviewPage::resolvePeerHostname(const char *peerIP)
{
  char *r;

  ink_mutex_acquire(&accessLock);
  r = this->resolvePeerHostname_ml(peerIP);
  ink_mutex_release(&accessLock);

  return r;
}

// char* overviewPage::resolvePeerHostname_ml(char* peerIP)
//
// Resolves the peer the hostname from its IP address
//   The hostname is resolved by finding the overviewRecord
//   Associated with the IP address and copying its hostname
//
// CALLEE frees storage
// CALLEE is responsible for locking
//
char *
overviewPage::resolvePeerHostname_ml(const char *peerIP)
{
  unsigned long int ipAddr;
  InkHashTableValue lookup;
  overviewRecord *peerRecord;
  char *returnName = NULL;

  ipAddr = inet_addr(peerIP);

  // Check to see if our address is malformed
  if ((long int)ipAddr == -1) {
    return NULL;
  }

  if (ink_hash_table_lookup(nodeRecords, (InkHashTableKey)ipAddr, &lookup)) {
    peerRecord = (overviewRecord *)lookup;
    returnName = ats_strdup(peerRecord->hostname);
  }

  return returnName;
}

// int hostSortFunc(const void* arg1, const void* arg2)
//
//   A compare function that we can to qsort that sorts
//    overviewRecord*
//
int
hostSortFunc(const void *arg1, const void *arg2)
{
  overviewRecord *rec1 = (overviewRecord *)*(void **)arg1;
  overviewRecord *rec2 = (overviewRecord *)*(void **)arg2;

  return strcmp(rec1->hostname, rec2->hostname);
}
