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

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "WebOverview.h"
#include "WebGlobals.h"
#include "WebHttpRender.h"
#include "WebHttpTree.h"
#include "WebMgmtUtils.h"

#include "Main.h"
#include "ClusterCom.h"
#include "MgmtDefs.h"
#include "CLI.h"
#include "CLIlineBuffer.h"
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
  } else {
    node_rec_data.num_recs = cpi->node_rec_data.num_recs;
    recordArraySize = node_rec_data.num_recs * sizeof(RecRecord);
    node_rec_data.recs = new RecRecord[recordArraySize];
    memcpy(node_rec_data.recs, cpi->node_rec_data.recs, recordArraySize);
  }


  // Query for the name of the node.  If it is not there, some
  //   their cluster ip address
  name_l = this->readString("proxy.node.hostname_FQ", &name_found);
  if (name_found == false || name_l == NULL) {
    nameFailed.s_addr = inetAddr;
    mgmt_log("[overviewRecord::overviewRecord] Unable to find hostname for %s\n", inet_ntoa(nameFailed));
    xfree(name_l);              // about to overwrite name_l, so we need to free it first
    name_l = xstrdup(inet_ntoa(nameFailed));
  }

  const size_t hostNameLen = strlen(name_l) + 1;
  this->hostname = new char[hostNameLen];
  ink_strncpy(this->hostname, name_l, hostNameLen);
  xfree(name_l);
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
overviewRecord::readCounter(char *name, bool * found)
{
  RecCounter rec = 0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordRelativeOrder(name, &order);
    if (rec_status == REC_ERR_OKAY) {
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
overviewRecord::readInteger(char *name, bool * found)
{
  RecInt rec = 0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordRelativeOrder(name, &order);
    if (rec_status == REC_ERR_OKAY) {
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

RecLLong
overviewRecord::readLLong(char *name, bool * found)
{
  RecLLong rec = 0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordRelativeOrder(name, &order);
    if (rec_status == REC_ERR_OKAY) {
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
      rec = node_rec_data.recs[order].data.rec_llong;
    }
  } else {
    rec_status = RecGetRecordLLong(name, &rec);
  }

  if (found) {
    *found = (rec_status == REC_ERR_OKAY);
  } else {
    mgmt_log(stderr, "node variables '%s' not found!\n");
  }
  return rec;
}

RecFloat
overviewRecord::readFloat(char *name, bool * found)
{
  RecFloat rec = 0.0;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordRelativeOrder(name, &order);
    if (rec_status == REC_ERR_OKAY) {
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
overviewRecord::readString(char *name, bool * found)
{
  RecString rec = NULL;
  int rec_status = REC_ERR_OKAY;
  int order = -1;
  if (localNode == false) {
    rec_status = RecGetRecordRelativeOrder(name, &order);
    if (rec_status == REC_ERR_OKAY) {
      ink_release_assert(order < node_rec_data.num_recs);
      ink_debug_assert(order < node_rec_data.num_recs);
      rec = xstrdup(node_rec_data.recs[order].data.rec_string);
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
overviewRecord::varStrFromName(char *varNameConst, char *bufVal, int bufLen)
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
    MgmtLLong llong_data;
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
  varName = xstrdup(varNameConst);
  varNameLen = strlen(varName);
  if (varNameLen > 3 && varName[varNameLen - 2] == '\\') {
    formatOption = varName[varNameLen - 1];

    // Now that we know the format option, terminate the string
    //   to make the option disappear
    varName[varNameLen - 2] = '\0';

    // Return not found for unknown format options
    if (formatOption != 'b' && formatOption != 'm' && formatOption != 'c' && formatOption != 'p') {
      xfree(varName);
      return false;
    }
  }
  if (RecGetRecordDataType(varName, &varDataType) == REC_ERR_FAIL) {
    xfree(varName);
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
      ink_sprintf(bufVal, "%lld", data.int_data);
    }
    break;
  case RECD_LLONG:
    data.llong_data = this->readLLong(varName, &found);
    if (formatOption == 'b') {
      bytesFromInt(data.llong_data, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt(data.int_data, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.int_data, bufVal);
    } else {
      ink_sprintf(bufVal, "%lld", data.int_data);
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
      ink_sprintf(bufVal, "%lld", data.counter_data);
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
    } else if (strlen(data.string_data) < (size_t) (bufLen - 1)) {
      ink_strncpy(bufVal, data.string_data, bufLen);
    } else {
      ink_strncpy(bufVal, data.string_data, bufLen);
    }
    xfree(data.string_data);
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  xfree(varName);
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

// void overviewPage::generateAlarmsTable(textBuffer* output)
//
//  places an HTML table containing
//
//    resolve   hostname  alarm_description into output
//
void
overviewPage::generateAlarmsTable(WebHttpContext * whc)
{

  overviewRecord *current;
  AlarmListable *curAlarm;
  char numBuf[256];
  char name[32];
  //  char *alarm_query;
  int alarm_count;

  textBuffer *output = whc->response_bdy;

  ink_mutex_acquire(&accessLock);

  // Iterate through each host
  alarm_count = 0;
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];

    // Iterate through the list of alarms
    curAlarm = current->nodeAlarms.head;

    while (curAlarm != NULL) {
      HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);

      // Hostname is an entry
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
      output->copyFrom(current->hostname, strlen(current->hostname));
      HtmlRndrTdClose(output);

      // Alarm description is an entry
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_TOP, NULL, NULL, 0);
      if (curAlarm->desc != NULL) {
        output->copyFrom(curAlarm->desc, strlen(curAlarm->desc));
      } else {
        const char *alarmText = lmgmt->alarm_keeper->getAlarmText(curAlarm->type);
        output->copyFrom(alarmText, strlen(alarmText));
      }
      HtmlRndrTdClose(output);

      // the name of each checkbox is : "alarm:<alarm_count>"
      // the value of each checkbox is: "<alarmId>:<ip addr>"
      HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_CENTER, HTML_VALIGN_NONE, NULL, NULL, 0);
      if (curAlarm->ip == NULL)
        snprintf(numBuf, sizeof(numBuf), "%d:local", curAlarm->type);
      else
        snprintf(numBuf, sizeof(numBuf), "%d:%s", curAlarm->type, curAlarm->ip);
      snprintf(name, sizeof(name), "alarm:%d", alarm_count);
      HtmlRndrInput(output, HTML_CSS_NONE, HTML_TYPE_CHECKBOX, name, numBuf, NULL, NULL);
      HtmlRndrTdClose(output);

      HtmlRndrTrClose(output);
      curAlarm = curAlarm->link.next;

      alarm_count++;

    }
  }

  ink_mutex_release(&accessLock);

  // check if we didn't find any alarms
  if (alarm_count == 0) {
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 3);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_NO_ACTIVE_ALARMS);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);
  }

}

void
overviewPage::generateAlarmsTableCLI(textBuffer * output)
{
  overviewRecord *current;
  AlarmListable *curAlarm;
  char ipBuf[64] = "";
  char alarmtypeBuf[32] = "";
  //  char            tmpBuf[256] = "";
  CLIlineBuffer *Obuf = NULL;
  char *out_buf = NULL;
  const char *field1 = " Alarm Id";
  const char *field2 = "Host";
  const char *field3 = "Alarm";

  ink_mutex_acquire(&accessLock);

  // Create Alarm header
  Obuf = new CLIlineBuffer(10);
  Obuf->addField("%-*s", field1, 21);
  Obuf->addField("%-*s", field2, 21);
  Obuf->addField("%-*s", field3, 30);
  out_buf = Obuf->getline();
  if (out_buf) {
    output->copyFrom(out_buf, strlen(out_buf));
    delete[]out_buf;
    out_buf = NULL;
  }
  Obuf->reset();
  output->copyFrom(CLI_globals::sep1, strlen(CLI_globals::sep1));

  // Iterate through each host
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];

    // Iterate through the list of alarms
    curAlarm = current->nodeAlarms.head;
    while (curAlarm != NULL) {
      //  
      //  The resolve input is <alarmId>:<ip addr>  
      //
      if (curAlarm->ip == NULL)
        snprintf(ipBuf, sizeof(ipBuf), "%s", "local");
      else
        snprintf(ipBuf, sizeof(ipBuf), "%s", curAlarm->ip);

      snprintf(alarmtypeBuf, sizeof(alarmtypeBuf), "%d", curAlarm->type);
      // alarm id field is 21
      Obuf->addField("%*s", alarmtypeBuf, 3);
      Obuf->addField("%-*s", ":", 1);
      Obuf->addField("%-*s", ipBuf, 16);
      Obuf->addField("%*s", " ", 1);
      // host field is 21
      // Hostname is an entry
      Obuf->addField("%-*s", current->hostname, 20);
      Obuf->addField("%*s", " ", 1);

      // Alarm description is an entry
      if (curAlarm->desc != NULL) {     // Desc field is 30
        Obuf->addField("%-*s", curAlarm->desc, 30);
      } else {
        const char *alarmText = lmgmt->alarm_keeper->getAlarmText(curAlarm->type);
        Obuf->addField("%-*s", alarmText, 20);
      }

      out_buf = Obuf->getline();        // get formatted output
      if (out_buf) {            // copy to output buffer
        output->copyFrom(out_buf, strlen(out_buf));
        delete[]out_buf;        // need to free this every time
        out_buf = NULL;
      }
      Obuf->reset();            // reset every time i.e. reuse

      curAlarm = curAlarm->link.next;
    }                           // end while(.)
  }                             // end for(.)

  // cleanup 
  delete Obuf;

  ink_mutex_release(&accessLock);
}                               // end generateAlarmsTableCLI()

// void overviewPage::generateAlarmsSummary(textBuffer* output)
//
//  alarm summary information (Alarm! [X pending])
//
void
overviewPage::generateAlarmsSummary(WebHttpContext * whc)
{

  overviewRecord *current;
  AlarmListable *curAlarm;
  char buf[256];
  int alarm_count;
  char *alarm_link;

  textBuffer *output = whc->response_bdy;

  ink_mutex_acquire(&accessLock);

  // Iterate through each host
  alarm_count = 0;
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];
    // Iterate through the list of alarms
    curAlarm = current->nodeAlarms.head;
    while (curAlarm != NULL) {
      alarm_count++;
      curAlarm = curAlarm->link.next;
    }
  }

  ink_mutex_release(&accessLock);

  if (alarm_count > 0) {
    HtmlRndrTableOpen(output, "100%", 0, 0, 0);

    HtmlRndrTrOpen(output, HTML_CSS_ALARM_COLOR, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_GREY_LINKS, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, "30", 0);
    alarm_link = WebHttpGetLink_Xmalloc(HTML_ALARM_FILE);
    HtmlRndrAOpen(output, HTML_CSS_NONE, alarm_link, NULL);
    xfree(alarm_link);
    HtmlRndrSpace(output, 2);
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_ALARM);
    snprintf(buf, sizeof(buf), "! [%d ", alarm_count);
    output->copyFrom(buf, strlen(buf));
    HtmlRndrText(output, whc->lang_dict_ht, HTML_ID_PENDING);
    snprintf(buf, sizeof(buf), "]");
    output->copyFrom(buf, strlen(buf));
    HtmlRndrAClose(output);
    HtmlRndrTdClose(output);
    HtmlRndrTrClose(output);

    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE);
    HtmlRndrTdOpen(output, HTML_CSS_TERTIARY_COLOR, HTML_ALIGN_NONE, HTML_VALIGN_NONE, "1", "1", 0);
    HtmlRndrDotClear(output, 1, 1);
    HtmlRndrTdClose(output);

    HtmlRndrTrClose(output);

    HtmlRndrTableClose(output);

  }

}

// generates the table for the overview page 
//
//  the entries are   hostname, on/off, alarm
//
void
overviewPage::generateTable(WebHttpContext * whc)
{

  // Varariables for finding out information about a specific node
  overviewRecord *current;
  char *hostName;
  bool alarm;
  bool up;
  bool found;
  PowerLampState proxyUp;
  bool sslEnabled = false;
  const char refFormat[] = "%s://%s:%d%s";
  char refBuf[256];             // Buffer for link to other nodes
  char *domainStart;            // pointer to domain name part of hostna,e
  int hostLen;                  // length of the host only portion of the hostname
  char outBuf[16 + 1];
  MgmtInt objs, t_hit, t_miss;
  MgmtFloat ops, hits, mbps;
  //  char *alarm_link;

  textBuffer *output = whc->response_bdy;
  MgmtHashTable *dict_ht = whc->lang_dict_ht;

  // Check to see if SSL is enabled.  Do one check, and record
  //  the info since it can change from under us in the pContext
  //  structure and we at least want to be able to give a
  //  consistent view
  if (whc->server_state & WEB_HTTP_SERVER_STATE_SSL_ENABLED) {
    sslEnabled = true;
  }

  ink_mutex_acquire(&accessLock);
  for (int i = 0; i < numHosts; i++) {
    current = (overviewRecord *) sortRecords[i];
    current->getStatus(&hostName, &up, &alarm, &proxyUp);

    // no longer need 'if (alarm)' since we have the alarm bar now
    //if (alarm)
    //HtmlRndrTrOpen(output, HTML_CSS_ALARM_COLOR, HTML_ALIGN_CENTER);
    //else
    HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_CENTER);

    // Make the hostname non-qualified if we have a host name and not
    // an ip address
    if (isdigit(*hostName))
      domainStart = NULL;
    else
      domainStart = strchr(hostName, '.');

    if (domainStart == NULL)
      hostLen = strlen(hostName);
    else
      hostLen = domainStart - hostName;

    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    // INKqa01119  - remove the up check since we are currently
    // not sending heartbeat packets when the proxy is off
    // if (up == true && current->localNode == false) {
    if (current->localNode == false) {
      char *link = WebHttpGetLink_Xmalloc(HTML_DEFAULT_MONITOR_FILE);
      if (sslEnabled == false) {
        // coverity[non_const_printf_format_string]
        snprintf(refBuf, sizeof(refBuf), refFormat, "http", hostName, wGlobals.webPort, link);
      } else {
        // coverity[non_const_printf_format_string]
        snprintf(refBuf, sizeof(refBuf), refFormat, "https", hostName, wGlobals.webPort, link);
      }
      HtmlRndrAOpen(output, HTML_CSS_GRAPH, refBuf, NULL);
      output->copyFrom(hostName, hostLen);
      HtmlRndrAClose(output);
      xfree(link);
    } else {
      output->copyFrom(hostName, hostLen);
    }
    HtmlRndrTdClose(output);

    // Add On/Off light
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    switch (proxyUp) {
    case LAMP_ON:
      HtmlRndrText(output, dict_ht, HTML_ID_ON);
      break;
    case LAMP_OFF:
      HtmlRndrText(output, dict_ht, HTML_ID_OFF);
      break;
    case LAMP_WARNING:
      HtmlRndrText(output, dict_ht, HTML_ID_WARNING);
      break;
    }
    HtmlRndrTdClose(output);

    // objects served
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    current->varIntFromName("proxy.node.user_agents_total_documents_served", &objs);
    ink_snprintf(outBuf, sizeof(outBuf), "%.10b64d", objs);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // ops/sec
    if (up == true) {
      ops = current->readFloat("proxy.node.user_agent_xacts_per_second", &found);
    } else {
      // Always report zero for down nodes
      ops = 0;
      found = true;
    }
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    ink_snprintf(outBuf, 16, "%.2f", ops);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // hit rate
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    current->varFloatFromName("proxy.node.cache_hit_ratio_avg_10s", &hits);
    ink_snprintf(outBuf, sizeof(outBuf), "%.2f%% ", hits * 100.0);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // throughput
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    current->varFloatFromName("proxy.node.client_throughput_out", &mbps);
    ink_snprintf(outBuf, sizeof(outBuf), "%.2f", mbps);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // hit latency
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    current->varIntFromName("proxy.node.http.transaction_msec_avg_10s.hit_fresh", &t_hit);
    ink_snprintf(outBuf, sizeof(outBuf), "%lld", t_hit);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // miss latency
    HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
    current->varIntFromName("proxy.node.http.transaction_msec_avg_10s.miss_cold", &t_miss);
    ink_snprintf(outBuf, sizeof(outBuf), "%lld", t_miss);
    output->copyFrom(outBuf, strlen(outBuf));
    HtmlRndrTdClose(output);

    // row close
    HtmlRndrTrClose(output);

    // show the 'details' section on the dashboard
    if (whc->request_state & WEB_HTTP_STATE_MORE_DETAIL) {
      this->addHostPanel(whc, current);
    }

  }

  ink_mutex_release(&accessLock);
}

//
// generates the table for the dashboard page for CLI
//
void
overviewPage::generateTableCLI(textBuffer * output)
{
  // Varariables for finding out information about a specific node
  overviewRecord *current;
  char *hostName;
  bool alarm;
  bool up;
  bool found;
  PowerLampState proxyUp;
  MgmtInt docCount;
  MgmtInt loadMetric;
  const char sformat[] = "%-3d %-15s %-8s %-6s %12s %12s";
  const char nameFormat[] = "%s";
  const char nodeFormat[] = "%s";
  const char alarmFormat[] = "%s";
  char namebuf[64];
  char nodebuf[32];
  char alarmbuf[32];
  char docCountBuf[32];
  char loadMetricBuf[32];
  char tmpbuf[256];
  char *domainStart;            // pointer to domain name part of hostname
  int hostLen;                  // length of the host only portion of the hostname

  // This determine is 'details' is displayed
  //moreInfo = this->moreInfoButton(submission, output);

  // acquire overviewPage
  ink_mutex_acquire(&accessLock);

  // for each host we know about
  for (int i = 0; i < numHosts; i++) {  // get host status
    current = (overviewRecord *) sortRecords[i];
    current->getStatus(&hostName, &up, &alarm, &proxyUp);

    // Make the hostname non-qualified if we have
    //   a host name and not an ip address
    if (isdigit(*hostName))
      domainStart = NULL;
    else
      domainStart = strchr(hostName, '.');

    if (domainStart == NULL)
      hostLen = strlen(hostName);
    else
      hostLen = domainStart - hostName;

    ink_snprintf(namebuf, hostLen + 1, nameFormat, hostName);
    namebuf[hostLen] = '\0';

    // Add On/Off/Down status
    switch (proxyUp) {
    case LAMP_ON:
      // coverity[non_const_printf_format_string]
      snprintf(nodebuf, sizeof(nodebuf), nodeFormat, "ON");
      break;
    case LAMP_OFF:
      // coverity[non_const_printf_format_string]
      snprintf(nodebuf, sizeof(nodebuf), nodeFormat, "OFF");
      break;
    case LAMP_WARNING:         // means node is down and not clustering
      // coverity[non_const_printf_format_string]
      snprintf(nodebuf, sizeof(nodebuf), nodeFormat, "DOWN");
      break;
      // no default:
    }

    // Add Alarm status
    if (alarm == true) {
      // coverity[non_const_printf_format_string]
      snprintf(alarmbuf, sizeof(alarmbuf), alarmFormat, "ALARM");
    } else {
      // coverity[non_const_printf_format_string]
      snprintf(alarmbuf, sizeof(alarmbuf), alarmFormat, "-");
    }

    // Add document count
    docCount = current->readInteger("proxy.node.user_agents_total_documents_served", &found);

    if (false == found) {
      docCount = 0;
    }
    // Transactions per/sec
    if (up == true) {
      loadMetric = (MgmtInt)
        current->readFloat("proxy.node.user_agent_xacts_per_second", &found);
    } else {
      // Always report zero for down nodes
      loadMetric = 0;
      found = true;
    }

    ink_snprintf(docCountBuf, sizeof(docCountBuf), "%lld", docCount);
    ink_snprintf(loadMetricBuf, sizeof(loadMetricBuf), "%lld", loadMetric);

    // create output status line
    // coverity[non_const_printf_format_string]
    snprintf(tmpbuf, sizeof(tmpbuf), sformat, i, namebuf, nodebuf, alarmbuf, docCountBuf, loadMetricBuf);
    output->copyFrom(tmpbuf, strlen(tmpbuf));

    output->copyFrom("\n", strlen("\n"));
  }                             // end for(..)

  // Add the Virtual IP Panel in the more detailed display

  ink_mutex_release(&accessLock);
}                               // end generateTableCLI()

// overviewPage::addHostPanel
//
//  Inserts stats entries for the host referenced by parameter host
//  Called by overviewPage::generateTable
//
void
overviewPage::addHostPanel(WebHttpContext * whc, overviewRecord * host)
{

  const char errorStr[] = "loading...";
  char tmp[256];
  in_addr ip;
  char *ip_str;

  textBuffer *output = whc->response_bdy;
  MgmtHashTable *dict_ht = whc->lang_dict_ht;

  //-----------------------------------------------------------------------
  // SET 1: CACHE TRANSACTION SUMMARY
  //-----------------------------------------------------------------------

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_NONE, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 8);

  MgmtFloat hits, hit_f, hit_r;
  MgmtFloat errs, abts, f;

  // get aborts
  abts = 0;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups", &f))
    abts += f;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups", &f))
    abts += f;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.early_hangups", &f))
    abts += f;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.aborts", &f))
    abts += f;

  // get errors
  errs = 0;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.connect_failed", &f))
    errs += f;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.errors.other", &f))
    errs += f;

  // get hits
  hits = hit_f = hit_r = 0;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.hit_fresh", &hit_f))
    hits += hit_f;
  if (host->varFloatFromName("proxy.node.http.transaction_frac_avg_10s.hit_revalidated", &hit_r))
    hits += hit_r;
#ifndef OLD_WAY
  host->varFloatFromName("proxy.node.cache_hit_ratio_avg_10s", &hits);
#endif /* !OLD_WAY */

#define SEPARATOR output->copyFrom("&nbsp;-&nbsp;", 13)

  HtmlRndrTableOpen(output, NULL, 0, 0, 0);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_CACHE_HIT_RATE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ink_snprintf(tmp, sizeof(tmp), "%.1f%% (%.1f%% ", hits * 100.0, hit_f * 100.0);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrText(output, dict_ht, HTML_ID_FRESH);
  ink_snprintf(tmp, sizeof(tmp), ", %.1f%% ", hit_r * 100.0);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrText(output, dict_ht, HTML_ID_REFRESH);
  output->copyFrom(")", 1);
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_ERRORS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ink_snprintf(tmp, sizeof(tmp), "%.1f%%", errs * 100.0);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_ABORTS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ink_snprintf(tmp, sizeof(tmp), "%.1f%%", abts * 100.0);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  //-----------------------------------------------------------------------
  // SET 2: ACTIVE CONNECTIONS
  //-----------------------------------------------------------------------

  MgmtInt clients, servers;

  clients = servers = 0;

  // TODO add NNTP/RNI here
  host->varIntFromName("proxy.node.current_client_connections", &clients);
  host->varIntFromName("proxy.node.current_server_connections", &servers);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_ACTIVE_CLIENTS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ink_snprintf(tmp, sizeof(tmp), "%lld", clients);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_ACTIVE_SERVERS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ink_snprintf(tmp, sizeof(tmp), "%lld", servers);
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  //-----------------------------------------------------------------------
  // SET 3: CLUSTER ADDRESS
  //-----------------------------------------------------------------------

  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_NODE_IP_ADDRESS);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  ip.s_addr = host->inetAddr;
  ip_str = inet_ntoa(ip);
  output->copyFrom(ip_str, strlen(ip_str));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  //-----------------------------------------------------------------------
  // SET 4: TS Lite / RNI
  //-----------------------------------------------------------------------

  if (host->varStrFromName("proxy.node.cache.bytes_free\\b", tmp, 256) == false) {
    ink_strncpy(tmp, errorStr, sizeof(tmp));
  }
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_CACHE_FREE_SPACE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  if (host->varStrFromName("proxy.node.hostdb.hit_ratio_avg_10s\\p", tmp, 256) == false) {
    ink_strncpy(tmp, errorStr, sizeof(tmp));
  }
  HtmlRndrTrOpen(output, HTML_CSS_NONE, HTML_ALIGN_LEFT);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  HtmlRndrText(output, dict_ht, HTML_ID_HOSTDB_HIT_RATE);
  HtmlRndrTdClose(output);
  HtmlRndrTdOpen(output, HTML_CSS_BODY_TEXT, HTML_ALIGN_NONE, HTML_VALIGN_NONE, NULL, NULL, 0);
  SEPARATOR;
  output->copyFrom(tmp, strlen(tmp));
  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

  HtmlRndrTableClose(output);

  HtmlRndrTdClose(output);
  HtmlRndrTrClose(output);

#undef SEPARATOR

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
    hosts->addEntry(xstrdup(current->hostname));
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
overviewPage::readString(const char *nodeName, char *name, bool * found)
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
overviewPage::readInteger(const char *nodeName, char *name, bool * found)
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
overviewPage::readFloat(const char *nodeName, char *name, bool * found)
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
//   Updates OLD proxy.cluster.http.cache_hit_ratio
//               proxy.cluster.http.cache_total_hits
//
//           NEW proxy.cluster.cache_hit_ratio
//               proxy.cluster.cache_total_hits
//
void
overviewPage::agCacheHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples cluster_hit_count = { "proxy.node.cache_total_hits", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_miss_count = { "proxy.node.cache_total_misses", 0, 0, 0, 0 };
  static char *cluster_hit_count_name = "proxy.cluster.cache_total_hits_avg_10s";
  static char *cluster_miss_count_name = "proxy.cluster.cache_total_misses_avg_10s";

  MgmtIntCounter totalHits = 0;
  MgmtIntCounter totalMisses = 0;
  MgmtIntCounter totalAccess = 0;
  MgmtFloat hitRate = 0.00;
  int status;

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
    status = clusterSumInt(cluster_hit_count.lm_record_name, &(cluster_hit_count.current_value));

    cluster_miss_count.current_value = -10000;
    cluster_miss_count.current_time = ink_get_hrtime();
    status = clusterSumInt(cluster_miss_count.lm_record_name, &(cluster_miss_count.current_value));

    ////////////////////////////////////////////////
    // if not initial or wrap, set derived values //
    ////////////////////////////////////////////////
    if ((current_time - last_set_time) > window) {
      RecInt num_hits = 0;
      RecInt num_misses = 0;
      RecInt diff = 0;
      RecInt total = 0;
      // generate time window deltas and sum
      diff = cluster_hit_count.diff_value();
      varSetInt(cluster_hit_count_name, diff);
      num_hits = diff;

      diff = cluster_miss_count.diff_value();
      varSetInt(cluster_miss_count_name, diff);
      num_misses = diff;

      total = num_hits + num_misses;
      if (total == 0)
        hitRate = 0.00;
      else
        hitRate = (MgmtFloat) ((double) num_hits / (double) total);

      // Check if more than one cluster node
      MgmtInt num_nodes;
      varIntFromName("proxy.process.cluster.nodes", &num_nodes);
      if (1 == num_nodes) {
        // Only one node , so grab local value 
        varFloatFromName("proxy.node.cache_hit_ratio_avg_10s", &hitRate);
      }
      // new stat
      varSetFloat("proxy.cluster.cache_hit_ratio_avg_10s", hitRate);
    }
    /////////////////////////////////////////////////
    // done with a cycle, update the last_set_time //
    /////////////////////////////////////////////////
    last_set_time = current_time;
  }
  // Deal with Lifetime stats
  clusterSumInt("proxy.node.cache_total_hits", &totalHits);
  clusterSumInt("proxy.node.cache_total_misses", &totalMisses);
  totalAccess = totalHits + totalMisses;

  if (totalAccess != 0) {
    hitRate = (MgmtFloat) ((double) totalHits / (double) totalAccess);
  }
  // old stats
  ink_assert(varSetFloat("proxy.cluster.http.cache_hit_ratio", hitRate));
  ink_assert(varSetInt("proxy.cluster.http.cache_total_hits", totalHits));
  ink_assert(varSetInt("proxy.cluster.http.cache_total_misses", totalMisses));

  // new stats
  ink_assert(varSetFloat("proxy.cluster.cache_hit_ratio", hitRate));
  ink_assert(varSetInt("proxy.cluster.cache_total_hits", totalHits));
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
  static char *cluster_hostdb_total_lookups_name = "proxy.cluster.hostdb.total_lookups_avg_10s";
  static char *cluster_hostdb_hits_name = "proxy.cluster.hostdb.total_hits_avg_10s";

  RecInt hostDBtotal = 0;
  RecInt hostDBhits = 0;
  //  RecInt hostDBmisses = 0;
  RecInt dnsTotal = 0;
  RecFloat hitRate = 0.00;
  int status;

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
    status = clusterSumInt(cluster_hostdb_total_lookups.lm_record_name, &(cluster_hostdb_total_lookups.current_value));

    cluster_hostdb_hits.current_value = -10000;
    cluster_hostdb_hits.current_time = ink_get_hrtime();
    status = clusterSumInt(cluster_hostdb_hits.lm_record_name, &(cluster_hostdb_hits.current_value));

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
//   Updates proxy.cluster.http.bandwidth_hit_ratio
//
void
overviewPage::agBandwidthHitRate()
{
  static ink_hrtime last_set_time = 0;
  const ink_hrtime window = 10 * HRTIME_SECOND; // update every 10 seconds
  static StatTwoIntSamples cluster_ua_total_bytes = { "proxy.node.user_agent_total_bytes", 0, 0, 0, 0 };
  static StatTwoIntSamples cluster_os_total_bytes = { "proxy.node.origin_server_total_bytes", 0, 0, 0, 0 };
  static char *cluster_ua_total_bytes_name = "proxy.cluster.user_agent_total_bytes_avg_10s";
  static char *cluster_os_total_bytes_name = "proxy.cluster.origin_server_total_bytes_avg_10s";

  MgmtInt bytes;
  MgmtInt UA_total = 0;         // User Agent total
  MgmtInt OSPP_total = 0;       // Origin Server and Parent Proxy(?)
  MgmtFloat hitRate;
  MgmtInt totalHits = 0;
  MgmtInt cacheOn = 1;          // on by default
  MgmtInt httpCacheOn, ftpCacheOn;
  int status;

  // See if cache is on
  ink_assert(varIntFromName("proxy.config.http.cache.http", &httpCacheOn));
  ink_assert(varIntFromName("proxy.config.http.cache.ftp", &ftpCacheOn));
  cacheOn = httpCacheOn || ftpCacheOn;

  // Get total cluster hits first, only calculate bandwith if > 0
  varIntFromName("proxy.cluster.http.cache_total_hits", &totalHits);

  // User Agent 

  // HTTP
  varIntFromName("proxy.cluster.http.user_agent_total_request_bytes", &bytes);
  UA_total += bytes;
  varIntFromName("proxy.cluster.http.user_agent_total_response_bytes", &bytes);
  UA_total += bytes;

  // FTP
  varIntFromName("proxy.cluster.ftp.downstream_total_bytes", &bytes);
  UA_total += bytes;

  // NNTP
  varIntFromName("proxy.cluster.nntp.downstream_total_bytes", &bytes);
  UA_total += bytes;


  // Origin Server and Parent Proxy

  // HTTP
  varIntFromName("proxy.cluster.http.origin_server_total_request_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.origin_server_total_response_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.parent_proxy_total_request_bytes", &bytes);
  OSPP_total += bytes;
  varIntFromName("proxy.cluster.http.parent_proxy_total_response_bytes", &bytes);
  OSPP_total += bytes;

  // FTP
  varIntFromName("proxy.cluster.ftp.upstream_total_bytes", &bytes);
  OSPP_total += bytes;

  // NNTP
  varIntFromName("proxy.cluster.nntp.upstream_total_bytes", &bytes);
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
    // old stat
    ink_assert(varSetFloat("proxy.cluster.http.bandwidth_hit_ratio", hitRate));

    // new stat
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
    status = clusterSumInt(cluster_ua_total_bytes.lm_record_name, &(cluster_ua_total_bytes.current_value));

    cluster_os_total_bytes.current_value = -10000;
    cluster_os_total_bytes.current_time = ink_get_hrtime();
    status = clusterSumInt(cluster_os_total_bytes.lm_record_name, &(cluster_os_total_bytes.current_value));

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
overviewPage::clusterSumInt(char *nodeVar, RecInt * sum)
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
overviewPage::clusterAgInt(char *clusterVar, char *nodeVar)
{
  int numUsed = 0;
  MgmtInt sum = 0;

  numUsed = clusterSumInt(nodeVar, &sum);
  if (numUsed > 0) {
    ink_assert(varSetInt(clusterVar, sum));
  }
}

void
overviewPage::clusterAgIntScale(char *clusterVar, char *nodeVar, double factor)
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
overviewPage::clusterSumFloat(char *nodeVar, RecFloat * sum)
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
overviewPage::clusterAgFloat(char *clusterVar, char *nodeVar)
{
  int numUsed = 0;
  MgmtFloat sum = 0;

  numUsed = clusterSumFloat(nodeVar, &sum);

  if (numUsed > 0) {
    ink_assert(varSetFloat(clusterVar, sum));
  }
}

// int varClusterIntFromName(char* nodeVar, MgmtInt* sum)
//   External accessible wrapper for clusterIntFloat.
//   It acquires the overveiw access lock, calls the
//   clusterIntFloat function and then releases the lock.
int
overviewPage::varClusterIntFromName(char *nodeVar, RecInt * sum)
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
  *sum = (RecInt) tempFloat;
  ink_mutex_release(&accessLock);
  return (status);
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

int
overviewPage::varClusterCounterFromName(char *nodeVar, RecInt * sum)
{
  int status;
  ink_mutex_acquire(&accessLock);
  status = clusterSumCounter(nodeVar, sum);
  ink_mutex_release(&accessLock);
  return (status);
}

// void overviewPage::doClusterAg()
//
//   Aggregate data for cluster records
//
void
overviewPage::doClusterAg()
{

  ink_mutex_acquire(&accessLock);


  // DNS
  clusterAgFloat("proxy.cluster.dns.lookups_per_second", "proxy.node.dns.lookups_per_second");
  clusterAgInt("proxy.cluster.dns.total_dns_lookups", "proxy.node.dns.total_dns_lookups");
  // HTTP
  clusterAgInt("proxy.cluster.http.throughput", "proxy.node.http.throughput");

  clusterAgFloat("proxy.cluster.http.user_agent_xacts_per_second", "proxy.node.http.user_agent_xacts_per_second");

  clusterAgInt("proxy.cluster.http.user_agent_current_connections_count",
               "proxy.node.http.user_agent_current_connections_count");
  clusterAgInt("proxy.cluster.http.origin_server_current_connections_count",
               "proxy.node.http.origin_server_current_connections_count");
  clusterAgInt("proxy.cluster.http.cache_current_connections_count", "proxy.node.http.cache_current_connections_count");

  clusterAgInt("proxy.cluster.http.current_parent_proxy_connections",
               "proxy.node.http.current_parent_proxy_connections");

  clusterAgInt("proxy.cluster.http.user_agent_total_request_bytes", "proxy.node.http.user_agent_total_request_bytes");
  clusterAgInt("proxy.cluster.http.user_agent_total_response_bytes", "proxy.node.http.user_agent_total_response_bytes");
  clusterAgInt("proxy.cluster.http.origin_server_total_request_bytes",
               "proxy.node.http.origin_server_total_request_bytes");
  clusterAgInt("proxy.cluster.http.origin_server_total_response_bytes",
               "proxy.node.http.origin_server_total_response_bytes");
  clusterAgInt("proxy.cluster.http.parent_proxy_total_request_bytes",
               "proxy.node.http.parent_proxy_total_request_bytes");
  clusterAgInt("proxy.cluster.http.parent_proxy_total_response_bytes",
               "proxy.node.http.parent_proxy_total_response_bytes");

  clusterAgInt("proxy.cluster.http.user_agents_total_transactions_count",
               "proxy.node.http.user_agents_total_transactions_count");
  clusterAgInt("proxy.cluster.http.user_agents_total_documents_served",
               "proxy.node.http.user_agents_total_documents_served");
  clusterAgInt("proxy.cluster.http.origin_server_total_transactions_count",
               "proxy.node.http.origin_server_total_transactions_count");

  // NNTP
  clusterAgInt("proxy.cluster.nntp.upstream_total_bytes", "proxy.node.nntp.upstream_total_bytes");
  clusterAgInt("proxy.cluster.nntp.downstream_total_bytes", "proxy.node.nntp.downstream_total_bytes");
  clusterAgInt("proxy.cluster.nntp.current_server_connections", "proxy.node.nntp.current_server_connections");
  clusterAgInt("proxy.cluster.nntp.current_cache_connections", "proxy.node.nntp.current_cache_connections");
  clusterAgInt("proxy.cluster.nntp.user_agents_total_documents_served",
               "proxy.node.nntp.user_agents_total_documents_served");

  clusterAgFloat("proxy.cluster.nntp.user_agent_xacts_per_second", "proxy.node.nntp.user_agent_xacts_per_second");

  // FTP
  clusterAgInt("proxy.cluster.ftp.upstream_total_bytes", "proxy.node.ftp.upstream_total_bytes");
  clusterAgInt("proxy.cluster.ftp.downstream_total_bytes", "proxy.node.ftp.downstream_total_bytes");
  clusterAgInt("proxy.cluster.ftp.current_server_connections", "proxy.node.ftp.current_server_connections");
  clusterAgInt("proxy.cluster.ftp.current_client_connections", "proxy.node.ftp.current_client_connections");
  clusterAgInt("proxy.cluster.ftp.current_cache_connections", "proxy.node.ftp.current_cache_connections");
  clusterAgInt("proxy.cluster.ftp.user_agents_total_documents_served",
               "proxy.node.ftp.user_agents_total_documents_served");

  clusterAgFloat("proxy.cluster.ftp.user_agent_xacts_per_second", "proxy.node.ftp.user_agent_xacts_per_second");

  // RNI
  clusterAgInt("proxy.cluster.rni.upstream_total_bytes", "proxy.node.rni.upstream_total_bytes");
  clusterAgInt("proxy.cluster.rni.downstream_total_bytes", "proxy.node.rni.downstream_total_bytes");
  clusterAgInt("proxy.cluster.rni.current_server_connections", "proxy.node.rni.current_server_connections");
  clusterAgInt("proxy.cluster.rni.current_client_connections", "proxy.node.rni.current_client_connections");
  clusterAgInt("proxy.cluster.rni.current_cache_connections", "proxy.node.rni.current_cache_connections");
  clusterAgInt("proxy.cluster.rni.user_agents_total_documents_served",
               "proxy.node.rni.user_agents_total_documents_served");

  clusterAgFloat("proxy.cluster.rni.user_agent_xacts_per_second", "proxy.node.rni.user_agent_xacts_per_second");

  // WMT
  clusterAgInt("proxy.cluster.wmt.upstream_total_bytes", "proxy.node.wmt.upstream_total_bytes");
  clusterAgInt("proxy.cluster.wmt.downstream_total_bytes", "proxy.node.wmt.downstream_total_bytes");
  clusterAgInt("proxy.cluster.wmt.current_server_connections", "proxy.node.wmt.current_server_connections");
  clusterAgInt("proxy.cluster.wmt.current_client_connections", "proxy.node.wmt.current_client_connections");
  clusterAgInt("proxy.cluster.wmt.current_cache_connections", "proxy.node.wmt.current_cache_connections");
  clusterAgInt("proxy.cluster.wmt.user_agents_total_documents_served",
               "proxy.node.wmt.user_agents_total_documents_served");

  clusterAgFloat("proxy.cluster.wmt.user_agent_xacts_per_second", "proxy.node.wmt.user_agent_xacts_per_second");

  // QT
  clusterAgInt("proxy.cluster.qt.upstream_total_bytes", "proxy.node.qt.upstream_total_bytes");
  clusterAgInt("proxy.cluster.qt.downstream_total_bytes", "proxy.node.qt.downstream_total_bytes");
  clusterAgInt("proxy.cluster.qt.current_server_connections", "proxy.node.qt.current_server_connections");
  clusterAgInt("proxy.cluster.qt.current_client_connections", "proxy.node.qt.current_client_connections");
  clusterAgInt("proxy.cluster.qt.current_cache_connections", "proxy.node.qt.current_cache_connections");
  clusterAgInt("proxy.cluster.qt.user_agents_total_documents_served",
               "proxy.node.qt.user_agents_total_documents_served");

  clusterAgFloat("proxy.cluster.qt.user_agent_xacts_per_second", "proxy.node.qt.user_agent_xacts_per_second");


  // Cache
  clusterAgInt("proxy.cluster.cache.bytes_free", "proxy.node.cache.bytes_free");
  clusterAgIntScale("proxy.cluster.cache.bytes_free_mb", "proxy.node.cache.bytes_free", MB_SCALE);
  clusterAgInt("proxy.cluster.cache.contents.num_docs", "proxy.node.cache.contents.num_docs");

  this->agHostdbHitRate();
  this->agCacheHitRate();
  this->agCachePercentFree();
  this->agBandwidthHitRate();
  this->agConnections();

  // Overall
  clusterAgFloat("proxy.cluster.client_throughput_out", "proxy.node.client_throughput_out");

  // FIXME
  clusterAgFloat("proxy.cluster.user_agent_xacts_per_second", "proxy.node.user_agent_xacts_per_second");

  // This is here for benefit of SNMP.
  AgFloat_generic_scale_to_int("proxy.cluster.client_throughput_out",
                               "proxy.cluster.client_throughput_out_kbit", MBIT_TO_KBIT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.http.cache_hit_ratio",
                               "proxy.cluster.http.cache_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.cache_hit_ratio",
                               "proxy.cluster.cache_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.http.bandwidth_hit_ratio",
                               "proxy.cluster.http.bandwidth_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.bandwidth_hit_ratio",
                               "proxy.cluster.bandwidth_hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.hostdb.hit_ratio",
                               "proxy.cluster.hostdb.hit_ratio_int_pct", PCT_TO_INTPCT_SCALE);
  AgFloat_generic_scale_to_int("proxy.cluster.cache.percent_free",
                               "proxy.cluster.cache.percent_free_int_pct", PCT_TO_INTPCT_SCALE);
  ink_mutex_release(&accessLock);
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
    returnName = xstrdup(peerRecord->hostname);
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

void
resolveAlarmCLI(textBuffer * output, const char *request)
{
  Tokenizer colonTok(":");
  const char *ipAddr = NULL;
  alarm_t alarmType;

  // Get ipAddr of host to resolve alarm for 
  // request is in form 'alarmType:ipAddr'
  if (request && colonTok.Initialize(request) == 2) {
    alarmType = atoi(colonTok[0]);
    ipAddr = colonTok[1];

    Debug("cli", "resolveAlarmCLI: Resolving alarm %d for %s\n", alarmType, ipAddr);

    if (strcmp("local", ipAddr) == 0)
      ipAddr = NULL;

    if (lmgmt->alarm_keeper->isCurrentAlarm(alarmType, (char *) ipAddr)) {
      Debug("cli", "\t Before resolution the alarm is current\n");
    } else {
      Debug("cli", "\t Before resolution the alarm is NOT current\n");
    }

    lmgmt->alarm_keeper->resolveAlarm(alarmType, (char *) ipAddr);

    if (lmgmt->alarm_keeper->isCurrentAlarm(alarmType, (char *) ipAddr)) {
      Debug("cli", "\t After resolution the alarm is current\n");
    } else {
      Debug("cli", "\t After resolution the alarm is NOT current\n");
    }
  }

  overviewGenerator->checkForUpdates();
}                               // end resolveAlarmCLI()

//   wrapper for the Alarm Callback
void
overviewAlarmCallback(alarm_t newAlarm, char *ip, char *desc)
{
  overviewGenerator->addAlarm(newAlarm, ip, desc);
}

AlarmListable::~AlarmListable()
{
  if (ip != NULL) {
    xfree(ip);
  }

  if (desc != NULL) {
    xfree(desc);
  }
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
