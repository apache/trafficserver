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

#include "ink_platform.h"
#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "WebOverview.h"
#include "WebGlobals.h"
#include "WebMgmtUtils.h"

#include "LocalManager.h"
#include "ClusterCom.h"
#include "MgmtUtils.h"
#include "MgmtDefs.h"
#include "Diags.h"

// Make this pointer to avoid nasty destruction
//   problems do to alarm
//   fork, execl, exit squences
overviewPage *overviewGenerator;

overviewRecord::overviewRecord(unsigned long inet_addr, bool local, ClusterPeerInfo * cpi)
{

  char *name_l;                 // hostname looked up from node record
  bool name_found;
  struct in_addr nameFailed;

  inetAddr = inet_addr;

  this->up = false;
  this->localNode = local;

  // If this is the local node, there is no cluster peer info
  //   record.  Remote nodes require a cluster peer info record
  ink_assert((local == false && cpi != NULL)
             || (local == true && cpi == NULL));

  // Set up the copy of the records array and initialize it
  if (local == true) {
    node_rec_data.num_recs = 0;
    node_rec_data.recs = NULL;
    recordArraySize = 0;
    node_rec_first_ix = 0;
  } else {
    node_rec_data.num_recs = cpi->node_rec_data.num_recs;
    recordArraySize = node_rec_data.num_recs * sizeof(RecRecord);
    node_rec_data.recs = new RecRecord[recordArraySize];
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
    ats_free(name_l);              // about to overwrite name_l, so we need to free it first
    name_l = ats_strdup(inet_ntoa(nameFailed));
  }

  const size_t hostNameLen = strlen(name_l) + 1;
  this->hostname = new char[hostNameLen];
  ink_strlcpy(this->hostname, name_l, hostNameLen);
  ats_free(name_l);
}

overviewRecord::~overviewRecord()
{

  AlarmListable *a;

  delete[]hostname;

  for (a = nodeAlarms.pop(); a != NULL; a = nodeAlarms.pop()) {
    delete a;
  }

  if (localNode == false) {
    delete[]node_rec_data.recs;
  }
}

// void overviewRecord::getStatus(char**, PowerLampState* , bool*, bool*)
// Retrieves information about the node
//
//  hostname - *hostname is set to point to a string containing the hostname
//             for the node represented by the record The storage for
//             this string belongs to this class instance and should
//             not be freed by the caller
//
//  *up - set to true if the node's manager is up
//            Set to false otherwise
//
//  *alarms -  set to true if there are any pending alarms for this
//            this node.   Set to false if there are no pending
//            alarms
//
//  *proxyUp - set to true is the proxy is up on the node and
//            false otherwise
//
void
overviewRecord::getStatus(char **hostnamePtr, bool * upPtr, bool * alarms, PowerLampState * proxyUpPtr)
{
  bool found;
  *hostnamePtr = this->hostname;
  *upPtr = this->up;

  if (this->up != true) {
    *proxyUpPtr = LAMP_OFF;
  } else {
    if (this->readInteger("proxy.node.proxy_running", &found) != 1) {
      *proxyUpPtr = LAMP_OFF;
    } else {
      if (this->localNode == true) {
        // For the local node, make sure all the cluster connections
        //   are up.  If not issue a warning lamp
        if (lmgmt->clusterOk() == false) {
          *proxyUpPtr = LAMP_WARNING;
        } else {
          *proxyUpPtr = LAMP_ON;
        }
      } else {
        // We can not currently check remote node
        //  cluster info
        *proxyUpPtr = LAMP_ON;
      }
    }
  }

  if (nodeAlarms.head == NULL) {
    *alarms = false;
  } else {
    *alarms = true;
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
overviewRecord::updateStatus(time_t currentTime, ClusterPeerInfo * cpi)
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

// adds a new alarm to the list of current alarms for the node
void
overviewRecord::addAlarm(alarm_t type, char *ip, char *desc)
{

  AlarmListable *alarm;

  alarm = new AlarmListable;
  alarm->ip = ip;
  alarm->type = type;
  alarm->desc = desc;
  nodeAlarms.push(alarm);
}

// adds a new alarm to the list of current alarms for the node
void
overviewRecord::addAlarm(AlarmListable * newAlarm)
{
  nodeAlarms.push(newAlarm);
}

// bool overviewRecord::ipMatch(char* ipStr)
//
//   Returns true if the passed in string matches
//     the ip address for this node
bool
overviewRecord::ipMatch(char *ipStr)
{
  if (inet_addr(ipStr) == inetAddr) {
    return true;
  } else {
    return false;
  }
}

// Runs throught the list of current alarms on the node
//  and asks the Alarms class if it is valid.  If the alarm
//  is expired it is removed from the alarm list
void
overviewRecord::checkAlarms()
{

  AlarmListable *current;
  AlarmListable *next;

  current = nodeAlarms.head;
  while (current != NULL) {

    next = current->link.next;

    if (!lmgmt->alarm_keeper->isCurrentAlarm(current->type, current->ip)) {
      // The alarm is no longer current.  Dispose of it
      nodeAlarms.remove(current);
      delete current;
    }

    current = next;
  }
}

//  overview::readCounter, overview::readInteger
//  overview::readFloat, overview::readString
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
RecCounter
overviewRecord::readCounter(const char *name, bool * found)
{
  RecCounter rec = 0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
      rec = node_rec_data.recs[order].data.rec_counter;
    } else {
      mgmt_log(stderr, "node variables '%s' not found!\n");
    }
  }

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

RecInt
overviewRecord::readInteger(const char *name, bool * found)
{
  RecInt rec = 0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
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
overviewRecord::readFloat(const char *name, bool * found)
{
  RecFloat rec = 0.0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
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
overviewRecord::readString(const char *name, bool * found)
{
  RecString rec = NULL;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordOrderAndId(name, &order, NULL);
    if (rec_status == REC_ERR_OKAY) {
      order -= node_rec_first_ix; // Offset
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
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

// bool overviewRecord::varStrFromName (char*, char*bufVal, char*, int)
//
//  Accessor function for node records.  Looks up varName for
//    this node and if found, turns it value into a string
//    and places it in bufVal
//
//  return true if bufVal was succefully set
//    and false otherwise
//
//  EVIL ALERT: varStrFromName in WebMgmtUtils.cc is extremely
//    similar to this function except in how it gets it's
//    data.  Changes to this fuction must be propogated
//    to its twin.  Cut and Paste sucks but there is not
//    an easy way to merge the functions
//
bool
overviewRecord::varStrFromName(const char *varNameConst, char *bufVal, int bufLen)
{
  char *varName;
  RecDataT varDataType;
  bool found = true;
  int varNameLen;
  char formatOption = '\0';

  union
  {
    MgmtIntCounter counter_data;        /* Data */
    MgmtInt int_data;
    MgmtFloat float_data;
    MgmtString string_data;
  } data;

  // Check to see if there is a \ option on the end of variable
  //   \ options indicate that we need special formatting
  //   of the results.  Supported \ options are
  //
  ///  b - bytes.  Ints and Counts only.  Amounts are
  //       transformed into one of GB, MB, KB, or B
  //
  varName = ats_strdup(varNameConst);
  varNameLen = strlen(varName);
  if (varNameLen > 3 && varName[varNameLen - 2] == '\\') {
    formatOption = varName[varNameLen - 1];

    // Now that we know the format option, terminate the string
    //   to make the option disappear
    varName[varNameLen - 2] = '\0';

    // Return not found for unknown format options
    if (formatOption != 'b' && formatOption != 'm' && formatOption != 'c' && formatOption != 'p') {
      ats_free(varName);
      return false;
    }
  }
  if (RecGetRecordDataType(varName, &varDataType) == REC_ERR_FAIL) {
    ats_free(varName);
    return false;
  }

  switch (varDataType) {
  case RECD_INT:
    data.int_data = this->readInteger(varName, &found);
    if (formatOption == 'b') {
      bytesFromInt(data.int_data, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt(data.int_data, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.int_data, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%" PRId64 "", data.int_data);
    }
    break;
  case RECD_COUNTER:
    data.counter_data = this->readCounter(varName, &found);
    if (formatOption == 'b') {
      bytesFromInt((MgmtInt) data.counter_data, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt((MgmtInt) data.counter_data, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.counter_data, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%" PRId64 "", data.counter_data);
    }
    break;
  case RECD_FLOAT:
    data.float_data = this->readFloat(varName, &found);
    if (formatOption == 'p') {
      percentStrFromFloat(data.float_data, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%.2f", data.float_data);
    }
    break;
  case RECD_STRING:
    data.string_data = this->readString(varName, &found);
    if (data.string_data == NULL) {
      bufVal[0] = '\0';
    } else {
      ink_strlcpy(bufVal, data.string_data, bufLen);
    }
    ats_free(data.string_data);
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  ats_free(varName);
  return found;
}

bool
overviewRecord::varCounterFromName(const char *name, MgmtIntCounter * value)
{
  bool found = false;

  if (value)
    *value = readCounter((char *) name, &found);
  return found;
}

bool
overviewRecord::varIntFromName(const char *name, MgmtInt * value)
{
  bool found = false;

  if (value)
    *value = readInteger((char *) name, &found);
  return found;
}

bool
overviewRecord::varFloatFromName(const char *name, MgmtFloat * value)
{
  bool found = false;

  if (value)
    *value = readFloat((char *) name, &found);

  return found;
}

overviewPage::overviewPage():sortRecords(10, false)
{

  ink_mutex_init(&accessLock, "overviewRecord");
  nodeRecords = ink_hash_table_create(InkHashTableKeyType_Word);
  numHosts = 0;
  ourAddr = 0;                  // We will update this when we add the record for
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
  for (entry = ink_hash_table_iterator_first(lmgmt->ccom->peers, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(lmgmt->ccom->peers, &iterator_state)) {

    tmp = (ClusterPeerInfo *) ink_hash_table_entry_value(lmgmt->ccom->peers, entry);

    if (ink_hash_table_lookup(nodeRecords, (InkHashTableKey) tmp->inet_address, (InkHashTableValue *) & current) == 0) {
      this->addRecord(tmp);
      newHostAdded = true;
    } else {
      current->updateStatus(currentTime, tmp);
    }
  }
  ink_mutex_release(&lmgmt->ccom->mutex);

  // Now check to see if our alarms up to date
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];
    current->checkAlarms();
  }

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
overviewPage::addRecord(ClusterPeerInfo * cpi)
{

  overviewRecord *newRec;

  AlarmListable *current;
  AlarmListable *next;

  ink_assert(cpi != NULL);

  newRec = new overviewRecord(cpi->inet_address, false, cpi);
  newRec->updateStatus(time(NULL), cpi);

  ink_hash_table_insert(nodeRecords, (InkHashTableKey) cpi->inet_address, (InkHashTableEntry *) newRec);

  // Check to see if we have alarms that need to be added
  //
  //  This an inefficient linear search, however there should
  //    never be a large number of alarms that do not
  //    nodes yet.  This should only happen at start up
  //
  current = notFoundAlarms.head;
  while (current != NULL) {

    next = current->link.next;

    if (newRec->ipMatch(current->ip) == true) {
      // The alarm belongs to this record, remove it and
      //    add it to the record
      notFoundAlarms.remove(current);
      newRec->addAlarm(current);
    }

    current = next;
  }

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
  AlarmListable *current;
  AlarmListable *next;

  ink_mutex_acquire(&accessLock);

  // We should not have been called before
  ink_assert(ourAddr == 0);

  // Find out what our cluster addr is from
  //   from cluster com
  this->ourAddr = lmgmt->ccom->getIP();

  newRec = new overviewRecord(ourAddr, true);
  newRec->up = true;

  ink_hash_table_insert(nodeRecords, (InkHashTableKey) this->ourAddr, (InkHashTableEntry *) newRec);

  // Check to see if we have alarms that need to be added
  //   They would be listed for IP zero since the alarm
  //   manager knows ip address for the local node as NULL
  //
  current = notFoundAlarms.head;
  while (current != NULL) {

    next = current->link.next;

    if (current->ip == NULL) {
      // The alarm belongs to this record, remove it and
      //    add it to the record
      notFoundAlarms.remove(current);
      newRec->addAlarm(current);
    }

    current = next;
  }

  sortRecords.addEntry(newRec);
  numHosts++;
  ink_mutex_release(&accessLock);
}

// adds alarm to the node specified by the ip address
//   if ip is NULL, the node is local machine
void
overviewPage::addAlarm(alarm_t type, char *ip, char *desc)
{

  unsigned long inetAddr;
  InkHashTableValue lookup;
  overviewRecord *node;
  AlarmListable *alarm;

  ink_mutex_acquire(&accessLock);

  if (ip == NULL) {
    inetAddr = ourAddr;
  } else {
    inetAddr = inet_addr(ip);
  }

  if (ink_hash_table_lookup(nodeRecords, (InkHashTableKey) inetAddr, &lookup)) {
    // We found our entry
    node = (overviewRecord *) lookup;
    node->addAlarm(type, ip, desc);
  } else {

    Debug("dashboard", "[overviewRecord::addAlarm] Alarm for node that we have not seen %s\n", ip);

    // If we have not seen the node, queue the alarm.  The node
    //  should appear eventually
    alarm = new AlarmListable;
    alarm->ip = ip;
    alarm->type = type;
    alarm->desc = desc;
    notFoundAlarms.push(alarm);
  }

  ink_mutex_release(&accessLock);
}


// int overviewPage::getClusterHosts(Expanding Array* hosts)
//
//   The names of all the cluster members are inserted
//     into parameter hosts.  The callee is responsible
//     for freeing the strings
//
int
overviewPage::getClusterHosts(ExpandingArray * hosts)
{
  int number = 0;

  overviewRecord *current;

  ink_mutex_acquire(&accessLock);
  number = sortRecords.getNumEntries();

  for (int i = 0; i < number; i++) {
    current = (overviewRecord *) sortRecords[i];
    hosts->addEntry(ats_strdup(current->hostname));
  }

  ink_mutex_release(&accessLock);
  return number;
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
  bool nodeFound = false;

  // Do a linear search of the nodes for this nodeName.
  //   Yes, I know this is slow but the current word is ten
  //   nodes would be a huge cluster so this should not
  //   be a problem
  //
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];
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
overviewPage::readString(const char *nodeName, const char *name, bool * found)
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
overviewPage::readInteger(const char *nodeName, const char *name, bool * found)
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
overviewPage::readFloat(const char *nodeName, const char *name, bool * found)
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

// void overviewPage::agCachePercentFree()
//
//  Updates proxy.cluster.cache.percent_free
//
void
overviewPage::agCachePercentFree()
{
  MgmtInt bTotal;
  MgmtInt bFree;
  MgmtFloat pFree;

  clusterSumInt("proxy.node.cache.bytes_total", &bTotal);
  clusterSumInt("proxy.node.cache.bytes_free", &bFree);

  if (bTotal <= 0) {
    pFree = 0.0;
  } else {
    pFree = (MgmtFloat) ((double) bFree / (double) bTotal);
  }

  ink_assert(varSetFloat("proxy.cluster.cache.percent_free", pFree));
}

// void overviewPage::agCacheHitRate()
//
void
overviewPage::agCacheHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples cluster_hit_count = { "proxy.node.cache_total_hits", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_hit_mem_count = { "proxy.node.cache_total_hits_mem", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_miss_count = { "proxy.node.cache_total_misses", 0, 0, 0, 0 };
  static const char *cluster_hit_count_name = "proxy.cluster.cache_total_hits_avg_10s";
  static const char *cluster_hit_mem_count_name = "proxy.cluster.cache_total_hits_mem_avg_10s";
  static const char *cluster_miss_count_name = "proxy.cluster.cache_total_misses_avg_10s";

  MgmtIntCounter totalHits = 0;
  MgmtIntCounter totalMemHits = 0;
  MgmtIntCounter totalMisses = 0;
  MgmtIntCounter totalAccess = 0;
  MgmtFloat hitRate = 0.00;
  MgmtFloat hitMemRate = 0.00;

  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();
  //  ink_hrtime delta = current_time - last_set_time;

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ////////////////////////////////////////
    // scroll values for cluster Hit/Miss //
    ///////////////////////////////////////
    cluster_hit_count.previous_time = cluster_hit_count.current_time;
    cluster_hit_count.previous_value = cluster_hit_count.current_value;

    cluster_miss_count.previous_time = cluster_miss_count.current_time;
    cluster_miss_count.previous_value = cluster_miss_count.current_value;

    //////////////////////////
    // calculate new values //
    //////////////////////////
    cluster_hit_count.current_value = -10000;
    cluster_hit_count.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_hit_count.lm_record_name, &(cluster_hit_count.current_value));

    cluster_miss_count.current_value = -10000;
    cluster_miss_count.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_miss_count.lm_record_name, &(cluster_miss_count.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      RecInt num_hits = 0;
      RecInt num_hits_mem = 0;
      RecInt num_misses = 0;
      RecInt diff = 0;
      RecInt total = 0;
      // generate time window deltas and sum
      diff = cluster_hit_count.diff_value();
      varSetInt(cluster_hit_count_name, diff);
      num_hits = diff;

      diff = cluster_hit_mem_count.diff_value();
      varSetInt(cluster_hit_mem_count_name, diff);
      num_hits_mem = diff;

      diff = cluster_miss_count.diff_value();
      varSetInt(cluster_miss_count_name, diff);
      num_misses = diff;

      total = num_hits + num_misses;
      if (total == 0)
        hitRate = 0.00;
      else {
        hitRate = (MgmtFloat) ((double) num_hits / (double) total);
        hitMemRate = (MgmtFloat) ((double) num_hits_mem / (double) total);
      }

      // Check if more than one cluster node
      MgmtInt num_nodes;
      varIntFromName("proxy.process.cluster.nodes", &num_nodes);
      if (1 == num_nodes) {
        // Only one node , so grab local value
        varFloatFromName("proxy.node.cache_hit_ratio_avg_10s", &hitRate);
        varFloatFromName("proxy.node.cache_hit_mem_ratio_avg_10s", &hitMemRate);
      }
      // new stat
      varSetFloat("proxy.cluster.cache_hit_ratio_avg_10s", hitRate);
      varSetFloat("proxy.cluster.cache_hit_mem_ratio_avg_10s", hitMemRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }
  // Deal with Lifetime stats
  clusterSumInt("proxy.node.cache_total_hits", &totalHits);
  clusterSumInt("proxy.node.cache_total_hits_mem", &totalMemHits);
  clusterSumInt("proxy.node.cache_total_misses", &totalMisses);
  totalAccess = totalHits + totalMisses;

  if (totalAccess != 0) {
    hitRate = (MgmtFloat) ((double) totalHits / (double) totalAccess);
    hitMemRate = (MgmtFloat) ((double) totalMemHits / (double) totalAccess);
  }
  // new stats
  ink_assert(varSetFloat("proxy.cluster.cache_hit_ratio", hitRate));
  ink_assert(varSetFloat("proxy.cluster.cache_hit_mem_ratio", hitMemRate));
  ink_assert(varSetInt("proxy.cluster.cache_total_hits", totalHits));
  ink_assert(varSetInt("proxy.cluster.cache_total_hits_mem", totalMemHits));
  ink_assert(varSetInt("proxy.cluster.cache_total_misses", totalMisses));
}

// void overviewPage::agHostDBHitRate()
//
//   Updates proxy.cluster.hostdb.hit_ratio
//
void
overviewPage::agHostdbHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples cluster_hostdb_total_lookups = { "proxy.node.hostdb.total_lookups", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_hostdb_hits = { "proxy.node.hostdb.total_hits", 0, 0, 0, 0 };
  static const char *cluster_hostdb_total_lookups_name = "proxy.cluster.hostdb.total_lookups_avg_10s";
  static const char *cluster_hostdb_hits_name = "proxy.cluster.hostdb.total_hits_avg_10s";

  RecInt hostDBtotal = 0;
  RecInt hostDBhits = 0;
  //  RecInt hostDBmisses = 0;
  RecInt dnsTotal = 0;
  RecFloat hitRate = 0.00;

  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();
  //  ink_hrtime delta = current_time - last_set_time;

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ////////////////////////////////////////
    // scroll values for cluster DNS //
    ///////////////////////////////////////
    cluster_hostdb_total_lookups.previous_time = cluster_hostdb_total_lookups.current_time;
    cluster_hostdb_total_lookups.previous_value = cluster_hostdb_total_lookups.current_value;

    cluster_hostdb_hits.previous_time = cluster_hostdb_hits.current_time;
    cluster_hostdb_hits.previous_value = cluster_hostdb_hits.current_value;

    //////////////////////////
    // calculate new values //
    //////////////////////////
    cluster_hostdb_total_lookups.current_value = -10000;
    cluster_hostdb_total_lookups.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_hostdb_total_lookups.lm_record_name, &(cluster_hostdb_total_lookups.current_value));

    cluster_hostdb_hits.current_value = -10000;
    cluster_hostdb_hits.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_hostdb_hits.lm_record_name, &(cluster_hostdb_hits.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      MgmtInt num_total_lookups = 0;
      MgmtInt num_hits = 0;
      MgmtInt diff = 0;

      // generate time window deltas and sum
      diff = cluster_hostdb_total_lookups.diff_value();
      varSetInt(cluster_hostdb_total_lookups_name, diff);
      num_total_lookups = diff;

      diff = cluster_hostdb_hits.diff_value();
      varSetInt(cluster_hostdb_hits_name, diff);
      num_hits = diff;

      if (num_total_lookups == 0)
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) ((double) num_hits / (double) num_total_lookups);

      // Check if more than one cluster node
      MgmtInt num_nodes;
      varIntFromName("proxy.process.cluster.nodes", &num_nodes);
      if (1 == num_nodes) {
        // Only one node , so grab local value
        varFloatFromName("proxy.node.hostdb.hit_ratio_avg_10s", &hitRate);
      }
      // new stat
      varSetFloat("proxy.cluster.hostdb.hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }

  // Deal with Lifetime stats
  clusterSumInt("proxy.node.hostdb.total_lookups", &hostDBtotal);
  clusterSumInt("proxy.node.dns.total_dns_lookups", &dnsTotal);
  clusterSumInt("proxy.node.hostdb.total_hits", &hostDBhits);

  if (hostDBtotal != 0) {
    if (hostDBhits < 0) {
      hostDBhits = 0;
      mgmt_log(stderr, "truncating hit_ratio from %d to 0\n", hostDBhits);
    }
    hitRate = (MgmtFloat) ((double) hostDBhits / (double) hostDBtotal);
  } else {
    hitRate = 0.0;
  }

  ink_assert(hitRate >= 0.0);
  ink_assert(varSetFloat("proxy.cluster.hostdb.hit_ratio", hitRate));
}

// void overviewPage::agBandwidthHitRate()
//
void
overviewPage::agBandwidthHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples cluster_ua_total_bytes = { "proxy.node.user_agent_total_bytes", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_os_total_bytes = { "proxy.node.origin_server_total_bytes", 0, 0, 0, 0 };
  static const char *cluster_ua_total_bytes_name = "proxy.cluster.user_agent_total_bytes_avg_10s";
  static const char *cluster_os_total_bytes_name = "proxy.cluster.origin_server_total_bytes_avg_10s";

  MgmtInt bytes;
  MgmtInt UA_total = 0;         // User Agent total
  MgmtInt OSPP_total = 0;       // Origin Server and Parent Proxy(?)
  MgmtFloat hitRate;
  MgmtInt totalHits = 0;
  MgmtInt cacheOn = 1;          // on by default
  MgmtInt httpCacheOn;

  // See if cache is on
  ink_assert(varIntFromName("proxy.config.http.cache.http", &httpCacheOn));
  cacheOn = httpCacheOn;

  // Get total cluster hits first, only calculate bandwith if > 0
  varIntFromName("proxy.cluster.cache_total_hits", &totalHits);

  // User Agent

  // HTTP
  varIntFromName("proxy.cluster.http.user_agent_total_request_bytes", &bytes);
  UA_total += bytes;
  varIntFromName("proxy.cluster.http.user_agent_total_response_bytes", &bytes);
  UA_total += bytes;

  // HTTP
  varIntFromName("proxy.cluster.http.origin_server_total_request_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.origin_server_total_response_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.parent_proxy_total_request_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.parent_proxy_total_response_bytes", &bytes);
  OSPP_total += bytes;

  // Special negative bandwidth scenario is treated here
  // See (Bug INKqa03094) and Ag_Bytes() in 'StatAggregation.cc'
  bool setBW = true;
  if (UA_total != 0 && totalHits && cacheOn) {
    hitRate = ((double) UA_total - (double) OSPP_total) / (double) UA_total;
    if (hitRate < 0.0)
      setBW = false;            // negative bandwidth scenario....
  } else {
    hitRate = 0.0;
  }

  if (setBW) {
    ink_assert(varSetFloat("proxy.cluster.bandwidth_hit_ratio", hitRate));
  }
  // get current time and delta to work with
  ink_hrtime current_time = ink_get_hrtime();
  //  ink_hrtime delta = current_time - last_set_time;

  ///////////////////////////////////////////////////////////////
  // if enough time expired, or first time, or wrapped around: //
  //  (1) scroll current value into previous value             //
  //  (2) calculate new current values                         //
  //  (3) only if proper time expired, set derived values      //
  ///////////////////////////////////////////////////////////////
  if (((current_time - last_set_time) > window) ||      // sufficient elapsed time
      (last_set_time == 0) ||   // first time
      (last_set_time > current_time))   // wrapped around
  {
    ////////////////////////////////////////
    // scroll values for node UA/OS bytes //
    ///////////////////////////////////////
    cluster_ua_total_bytes.previous_time = cluster_ua_total_bytes.current_time;
    cluster_ua_total_bytes.previous_value = cluster_ua_total_bytes.current_value;

    cluster_os_total_bytes.previous_time = cluster_os_total_bytes.current_time;
    cluster_os_total_bytes.previous_value = cluster_os_total_bytes.current_value;

    //////////////////////////
    // calculate new values //
    //////////////////////////
    cluster_ua_total_bytes.current_value = -10000;
    cluster_ua_total_bytes.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_ua_total_bytes.lm_record_name, &(cluster_ua_total_bytes.current_value));

    cluster_os_total_bytes.current_value = -10000;
    cluster_os_total_bytes.current_time = ink_get_hrtime();
    // TODO: Should we check return value?
    clusterSumInt(cluster_os_total_bytes.lm_record_name, &(cluster_os_total_bytes.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      RecInt num_ua_total = 0;
      RecInt num_os_total = 0;
      RecInt diff = 0;

      // generate time window deltas and sum
      diff = cluster_ua_total_bytes.diff_value();
      varSetInt(cluster_ua_total_bytes_name, diff);
      num_ua_total = diff;

      diff = cluster_os_total_bytes.diff_value();
      varSetInt(cluster_os_total_bytes_name, diff);
      num_os_total = diff;

      if (num_ua_total == 0 || (num_ua_total < num_os_total))
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) (((double) num_ua_total - (double) num_os_total) / (double) num_ua_total);

      // Check if more than one cluster node
      MgmtInt num_nodes;
      varIntFromName("proxy.process.cluster.nodes", &num_nodes);
      if (1 == num_nodes) {
        // Only one node , so grab local value
        varFloatFromName("proxy.node.bandwidth_hit_ratio_avg_10s", &hitRate);
      }
      // new stat
      varSetFloat("proxy.cluster.bandwidth_hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }

}                               // end overviewPage::agBandwidthHitRate()

// int overviewPage::clusterSumInt(char* nodeVar, MgmtInt* sum)
//
//   Sums nodeVar for every up node in the cluster and stores the
//     sum in *sum.  Returns the number of nodes summed over
//
//   CALLEE MUST HOLD this->accessLock
//
int
overviewPage::clusterSumInt(const char *nodeVar, RecInt * sum)
{
  int numUsed = 0;
  int numHosts_local = sortRecords.getNumEntries();
  overviewRecord *current;
  bool found;

  ink_assert(sum != NULL);
  *sum = 0;

  for (int i = 0; i < numHosts_local; i++) {
    current = (overviewRecord *) sortRecords[i];
    if (current->up == true) {
      numUsed++;
      *sum += current->readInteger(nodeVar, &found);
      if (found == false) {
      }
    }
  }

  return numUsed;
}

//
//   Updates proxy.cluster.current_client_connections
//   Updates proxy.cluster.current_server_connections
//   Updates proxy.cluster.current_cache_connections
//
void
overviewPage::agConnections()
{
  MgmtInt client_conn = 0;
  MgmtInt server_conn = 0;
  MgmtInt cache_conn = 0;

  clusterSumInt("proxy.node.current_client_connections", &client_conn);
  clusterSumInt("proxy.node.current_server_connections", &server_conn);
  clusterSumInt("proxy.node.current_cache_connections", &cache_conn);

  ink_assert(varSetInt("proxy.cluster.current_client_connections", client_conn));
  ink_assert(varSetInt("proxy.cluster.current_server_connections", server_conn));
  ink_assert(varSetInt("proxy.cluster.current_cache_connections", cache_conn));
}

// void overviewPage::clusterAgInt(const char* clusterVar, const char* nodeVar)
//
//   Updates clusterVar with the sum of nodeVar for every node in the
//      cluster
//   CALLEE MUST HOLD this->accessLock
//
void
overviewPage::clusterAgInt(const char *clusterVar, const char *nodeVar)
{
  int numUsed = 0;
  MgmtInt sum = 0;

  numUsed = clusterSumInt(nodeVar, &sum);
  if (numUsed > 0) {
    ink_assert(varSetInt(clusterVar, sum));
  }
}

void
overviewPage::clusterAgIntScale(const char *clusterVar, const char *nodeVar, double factor)
{
  int numUsed = 0;
  RecInt sum = 0;

  numUsed = clusterSumInt(nodeVar, &sum);
  if (numUsed > 0) {
    sum = (int) (sum * factor);
    ink_assert(varSetInt(clusterVar, sum));
  }
}

// int overviewPage::clusterSumCounter(char* nodeVar, MgmtIntCounter* sum)
//
//   Sums nodeVar for every up node in the cluster and stores the
//     sum in *sum.  Returns the number of nodes summed over
//
//   CALLEE MUST HOLD this->accessLock
//
int
overviewPage::clusterSumCounter(char *nodeVar, RecInt * sum)
{
  int numUsed = 0;
  int numHosts_local = sortRecords.getNumEntries();
  overviewRecord *current;
  bool found;

  ink_assert(sum != NULL);
  *sum = 0;

  for (int i = 0; i < numHosts_local; i++) {
    current = (overviewRecord *) sortRecords[i];
    if (current->up == true) {
      numUsed++;
      *sum += current->readCounter(nodeVar, &found);
      if (found == false) {
      }
    }
  }

  return numUsed;
}

// int overviewPage::clusterSumFloat(char* nodeVar, MgmtFloat* sum)
//
//   Sums nodeVar for every up node in the cluster and stores the
//     sum in *sum.  Returns the number of nodes summed over
//
//   CALLEE MUST HOLD this->accessLock
//
int
overviewPage::clusterSumFloat(const char *nodeVar, RecFloat * sum)
{
  int numUsed = 0;
  int numHosts_local = sortRecords.getNumEntries();
  overviewRecord *current;
  bool found;

  ink_assert(sum != NULL);
  *sum = 0.00;

  for (int i = 0; i < numHosts_local; i++) {
    current = (overviewRecord *) sortRecords[i];
    if (current->up == true) {
      numUsed++;
      *sum += current->readFloat(nodeVar, &found);
      if (found == false) {
      }
    }
  }
  return numUsed;
}


// void overviewPage::clusterAgFloat(const char* clusterVar, const char* nodeVar)
//
//
//   Sums nodeVar for every up node in the cluster and stores the
//     sum in sumVar
//
//   CALLEE MUST HOLD this->accessLock
//
void
overviewPage::clusterAgFloat(const char *clusterVar, const char *nodeVar)
{
  int numUsed = 0;
  MgmtFloat sum = 0;

  numUsed = clusterSumFloat(nodeVar, &sum);

  if (numUsed > 0) {
    ink_assert(varSetFloat(clusterVar, sum));
  }
}

int
overviewPage::varClusterFloatFromName(char *nodeVar, RecFloat * sum)
{
  ink_mutex_acquire(&accessLock);
  int status = 0;
  RecDataT varDataType;
  RecInt tempInt = 0;
  RecFloat tempFloat = 0.0;

  RecGetRecordDataType(nodeVar, &varDataType);

  if (varDataType == RECD_INT) {
    status = clusterSumInt(nodeVar, &tempInt);
    tempFloat = (RecFloat) tempInt;
  } else if (varDataType == RECD_FLOAT) {
    status = clusterSumFloat(nodeVar, &tempFloat);
  }

  *sum = tempFloat;
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
    tmp = tmp + 0.5;            // round up.
    varSetInt(nodeVar, (int) tmp);
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
  if ((long int) ipAddr == -1) {
    return NULL;
  }

  if (ink_hash_table_lookup(nodeRecords, (InkHashTableKey) ipAddr, &lookup)) {
    peerRecord = (overviewRecord *) lookup;
    returnName = ats_strdup(peerRecord->hostname);
  }

  return returnName;
}

// resolveAlarm
//
//   Handles the form submission for alarm resolution
//   uses the form arguments to call resolveAlarm.
//
//   Takes a hash-table returned by processFormSubmission
//
//   Note: resolving an alarm is asyncronous with the list of
//      alarms maintained in overviewRecords.  That list
//      is only updates when checkAlarms is called
//
void
resolveAlarm(InkHashTable * post_data_ht)
{

  InkHashTableIteratorState htis;
  InkHashTableEntry *hte;
  char *name;
  char *value;
  Tokenizer colonTok(":");
  const char *ipAddr;
  alarm_t alarmType;

  for (hte = ink_hash_table_iterator_first(post_data_ht, &htis);
       hte != NULL; hte = ink_hash_table_iterator_next(post_data_ht, &htis)) {
    name = (char *) ink_hash_table_entry_key(post_data_ht, hte);
    value = (char *) ink_hash_table_entry_value(post_data_ht, hte);
    if (strncmp(name, "alarm:", 6) != 0)
      continue;
    if (colonTok.Initialize(value) == 2) {
      alarmType = atoi(colonTok[0]);
      ipAddr = colonTok[1];
      Debug("dashboard", "Resolving alarm %d for %s\n", alarmType, ipAddr);
      if (strcmp("local", ipAddr) == 0)
        ipAddr = NULL;
      if (lmgmt->alarm_keeper->isCurrentAlarm(alarmType, (char *) ipAddr)) {
        Debug("dashboard", "\t Before resolution the alarm is current\n");
      } else {
        Debug("dashboard", "\t Before resolution the alarm is NOT current\n");
      }
      lmgmt->alarm_keeper->resolveAlarm(alarmType, (char *) ipAddr);
      if (lmgmt->alarm_keeper->isCurrentAlarm(alarmType, (char *) ipAddr)) {
        Debug("dashboard", "\t After resolution the alarm is current\n");
      } else {
        Debug("dashboard", "\t After resolution the alarm is NOT current\n");
      }
    }
  }
  overviewGenerator->checkForUpdates();
}

//   wrapper for the Alarm Callback
void
overviewAlarmCallback(alarm_t newAlarm, char *ip, char *desc)
{
  overviewGenerator->addAlarm(newAlarm, ip, desc);
}

AlarmListable::~AlarmListable()
{
  ats_free(ip);
  ats_free(desc);
}

// int hostSortFunc(const void* arg1, const void* arg2)
//
//   A compare function that we can to qsort that sorts
//    overviewRecord*
//
int
hostSortFunc(const void *arg1, const void *arg2)
{
  overviewRecord *rec1 = (overviewRecord *) * (void **) arg1;
  overviewRecord *rec2 = (overviewRecord *) * (void **) arg2;

  return strcmp(rec1->hostname, rec2->hostname);
}
