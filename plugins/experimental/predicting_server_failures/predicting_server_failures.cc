//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <inttypes.h>
#include <netinet/tcp.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace std;

// Plugin Name
static const char PLUGIN_NAME[] = "predicting_server_failures";

// Flag for continuously calculating statistics while running
static bool running = false;

// Custom log file
TSTextLogObject *logObj;
bool logIsWritable = false;

// Forward Declarations
static int cont_check_os_request(TSCont, TSEvent, void *);
void maintainRatesAndEMAs();
void printStats();
double calculateEMA(double current, double oldEMA, double period);

// Statistics for monitoring
static const char connectionsToOSName[] = "plugin.predicting_server_failures.connectionsToOS";
static int connectionsToOSStat          = 0;
static const char dataRateSumName[]     = "plugin.predicting_server_failures.dataRateSum";
static int datarateSumStat              = 0;
static const char ttfbSumName[]         = "plugin.predicting_server_failures.ttfbSum";
static int ttfbSumStat                  = 0;
static const char dataRateName[]        = "plugin.predicting_server_failures.dataRate";
static int dataRateStat                 = 0;
static const char ttfbName[]            = "plugin.predicting_server_failures.ttfb";
static int ttfbStat                     = 0;
static const char dataRateEma1Name[]    = "plugin.predicting_server_failures.dataRateEma1";
static int dataRateEma1Stat             = 0;
static const char ttfbEma1Name[]        = "plugin.predicting_server_failures.ttfbEma1";
static int ttfbEma1Stat                 = 0;
static const char dataRateEma5Name[]    = "plugin.predicting_server_failures.dataRateEma5";
static int dataRateEma5Stat             = 0;
static const char ttfbEma5Name[]        = "plugin.predicting_server_failures.ttfbEma5";
static int ttfbEma5Stat                 = 0;

// New Statistics
static const char connectionsToOSRateName[] = "plugin.predicting_server_failures.connectionsToOSRate";
static int connectionsToOSRateStat          = 0;

// Continuation wrapper class
class ContinuationWrapper
{
public:
  ContinuationWrapper() { _cont = TSContCreate(cont_check_os_request, NULL); }
  ~ContinuationWrapper() { TSContDestroy(_cont); }
  TSCont
  continuation() const
  {
    return _cont;
  }

private:
  TSCont _cont;
};

// Initialize plugin as remap plugin
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "Predicting Server Failures Plugin Initiated");

  // Create statistics
  connectionsToOSStat = TSStatCreate(connectionsToOSName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(connectionsToOSStat, 0);
  datarateSumStat = TSStatCreate(dataRateSumName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(datarateSumStat, 0);
  ttfbSumStat = TSStatCreate(ttfbSumName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(ttfbSumStat, 0);
  dataRateStat = TSStatCreate(dataRateName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(dataRateStat, 0);
  ttfbStat = TSStatCreate(ttfbName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(ttfbStat, 0);
  dataRateEma1Stat = TSStatCreate(dataRateEma1Name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(dataRateEma1Stat, 0);
  dataRateEma5Stat = TSStatCreate(dataRateEma5Name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(dataRateEma5Stat, 0);
  ttfbEma1Stat = TSStatCreate(ttfbEma1Name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(ttfbEma1Stat, 0);
  ttfbEma5Stat = TSStatCreate(ttfbEma5Name, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(ttfbEma5Stat, 0);

  // New statistics
  connectionsToOSRateStat = TSStatCreate(connectionsToOSRateName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  TSStatIntSet(connectionsToOSRateStat, 0);

  // Prepare data rate calculation method
  running = true;

  // Prepare ema calculation
  thread calculationsThread(maintainRatesAndEMAs);
  calculationsThread.detach();

  // Prepare stats print thread
  thread statsPrintThread(printStats);
  statsPrintThread.detach();

  // Create custom log file
  logObj = new TSTextLogObject();
  if (TSTextLogObjectCreate("psf_stats.log", TS_LOG_MODE_ADD_TIMESTAMP, logObj) == TS_ERROR) {
    TSError("%s Could not create psf_stats.log file", PLUGIN_NAME);
  } else {
    logIsWritable = true;
  }

  // Write headings for log file
  if (logIsWritable) {
    TSTextLogObjectWrite(
      *logObj, "Data rate | Connections to OS | TTFB | Data rate EMA1 | Data rate EMA5 | TTFB EMA1 | TTFB EMA5 | Server Conn Rate");
    TSTextLogObjectFlush(*logObj);
  }

  TSDebug(PLUGIN_NAME,
          "Data rate | Connections to OS | TTFB | Data rate EMA1 | Data rate EMA5 | TTFB EMA1 | TTFB EMA5 | Server Conn Rate");
  return TS_SUCCESS;
}

// New Instance to read nonexistant rules
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  ContinuationWrapper *cont = new ContinuationWrapper;
  *ih                       = static_cast<void *>(cont);
  return TS_SUCCESS;
}

// Do remap
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  // Add continuation hooks
  ContinuationWrapper *cont = static_cast<ContinuationWrapper *>(ih);
  TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, cont->continuation());
  TSHttpTxnHookAdd(rh, TS_HTTP_SEND_REQUEST_HDR_HOOK, cont->continuation());
  TSHttpTxnHookAdd(rh, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont->continuation());
  return TSREMAP_DID_REMAP;
}

// Stop running
void
TSRemapDone()
{
  // Destroy TextLogObject
  if (logIsWritable) {
    TSTextLogObjectWrite(*logObj, "End");
    TSTextLogObjectFlush(*logObj);
  }
  running = false;
  TSTextLogObjectDestroy(*logObj);
}

// Cleanup
void
TSRemapDeleteInstance(void *ih)
{
  TSDebug(PLUGIN_NAME, "Delete Instance");
  delete static_cast<ContinuationWrapper *>(ih);
}

// Continuation
static int
cont_check_os_request(TSCont contp, TSEvent event, void *edata)
{
  // Transaction pointer
  TSHttpTxn txnp;

  // High resolution times used to timestamp specific milestones in the transaction
  TSHRTime *serverBeginWrite, *serverClose, *serverBeginRead, *serverFirstConnect;

  // Used to see differences between timestamps
  long long int beginWriteToClose, firstConnectToFirstRead;

  // Used for parsing header for info
  TSMBuffer bufp;
  TSMLoc offset;

  switch (event) {
  // Maintain connections to origin server statistic
  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    txnp = (TSHttpTxn)edata;
    TSStatIntIncrement(connectionsToOSStat, 1);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;

  // Maintain ttfb stat
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    txnp               = (TSHttpTxn)edata;
    serverFirstConnect = new TSHRTime();
    serverBeginRead    = new TSHRTime();

    if (TS_SUCCESS != TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_SERVER_FIRST_CONNECT, serverFirstConnect)) {
      TSDebug(PLUGIN_NAME, "Error getting milestone: SERVER_FIRST_CONNECT");
    }
    if (TS_SUCCESS != TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_SERVER_FIRST_READ, serverBeginRead)) {
      TSDebug(PLUGIN_NAME, "Error getting milestone: SERVER_READ_HEADER_DONE");
    }

    firstConnectToFirstRead = (*serverBeginRead - *serverFirstConnect); // Measured in ns

    // Get First Write to First Read Stat
    TSStatIntIncrement(ttfbSumStat,
                       static_cast<int>(firstConnectToFirstRead / 1000)); // Measured in µs (2.1B µs is 35 minutes)
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;

  // Maintain conn to OS stat and calc data rate
  case TS_EVENT_HTTP_TXN_CLOSE: {
    TSStatIntDecrement(connectionsToOSStat, 1);
    txnp             = (TSHttpTxn)edata;
    serverBeginWrite = new TSHRTime();
    serverClose      = new TSHRTime();
    // Get milestone time stamps for data rate
    if (TS_SUCCESS != TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_SERVER_BEGIN_WRITE, serverBeginWrite)) {
      TSDebug(PLUGIN_NAME, "Error getting milestone: SERVER_BEGIN_WRITE");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    if (TS_SUCCESS != TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_SERVER_CLOSE, serverClose)) {
      TSDebug(PLUGIN_NAME, "Error getting milestone: SERVER_READ_HEADER_DONE");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    // Calculate differences between time stamps
    beginWriteToClose = (*serverClose - *serverBeginWrite); // Measured in ns

    // Get server response
    if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &bufp, &offset)) {
      TSDebug(PLUGIN_NAME, "Error getting server response");
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    // Get content length field from server response header
    TSMLoc clen_field;
    clen_field  = TSMimeHdrFieldFind(bufp, offset, TS_MIME_FIELD_CONTENT_LENGTH, 0);
    long length = 0;
    if (TS_NULL_MLOC != clen_field) {
      length = static_cast<long>(TSMimeHdrFieldValueIntGet(bufp, offset, clen_field, -1)); // Measured in bytes
    }

    // Get data rate ratio
    float dataRateRatio = (static_cast<float>(length) / static_cast<float>(beginWriteToClose)) * 1000000000; // Measured in B/s
    TSStatIntIncrement(datarateSumStat, static_cast<int>(dataRateRatio));

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;

  default:
    txnp = (TSHttpTxn)edata;
    TSDebug(PLUGIN_NAME, "Remap Plugin: Fell into default case");
    TSDebug(PLUGIN_NAME, "%d", event);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  return 0;
}

void
maintainRatesAndEMAs()
{
  int period = 5000; // Milliseconds
  int i      = 0;

  while (running) {
    // Get initial sum values
    int drSum1   = TSStatIntGet(datarateSumStat);
    int ttfbSum1 = TSStatIntGet(ttfbSumStat);
    // Get initial connections
    int serverConn1 = TSStatIntGet(connectionsToOSStat);
    // Get initial MAs
    int ttfbEMA      = TSStatIntGet(ttfbEma1Stat);
    int dataRateEMA  = TSStatIntGet(dataRateEma1Stat);
    int dataRateEMA5 = TSStatIntGet(dataRateEma5Stat);
    // Check for cross condition 1 (DR EMA 1 > DR EMA 5)
    bool crossCondition1 = dataRateEMA > dataRateEMA5;

    // Wait
    this_thread::sleep_for(std::chrono::milliseconds(period));

    // Get new sum values
    int drSum2      = TSStatIntGet(datarateSumStat);
    int ttfbSum2    = TSStatIntGet(ttfbSumStat);
    int serverConn2 = TSStatIntGet(connectionsToOSStat);
    // Get data rates
    dataRateEMA5 = TSStatIntGet(dataRateEma5Stat);
    int ttfbEMA5 = TSStatIntGet(ttfbEma5Stat);

    // Calculate slopes
    int dataRate       = (drSum2 - drSum1) / (period / 1000);
    int ttfb           = (ttfbSum2 - ttfbSum1) / (period / 1000);
    int serverConnRate = (serverConn2 - serverConn1) / (period / 1000);
    TSStatIntSet(dataRateStat, dataRate);
    TSStatIntSet(ttfbStat, ttfb);
    // Check that it will not be < 0 to protect against underflow
    if (serverConnRate < 0) {
      serverConnRate = 0;
    }
    TSStatIntSet(connectionsToOSRateStat, serverConnRate);

    // Calculate and set new MAs
    ttfbEMA = static_cast<int>(
      calculateEMA(static_cast<double>(TSStatIntGet(ttfbStat)), static_cast<double>(ttfbEMA), static_cast<double>(period / 1000)));
    dataRateEMA = static_cast<int>(calculateEMA(static_cast<double>(TSStatIntGet(dataRateStat)), static_cast<double>(dataRateEMA),
                                                static_cast<double>(period / 1000)));
    TSStatIntSet(ttfbEma1Stat, ttfbEMA);
    TSStatIntSet(dataRateEma1Stat, dataRateEMA);
    i++;

    // Every 5th period (25000ms or 25s), calculate ema5s
    if (i == 4) {
      // Calculate EMA5s
      dataRateEMA5 = static_cast<int>(calculateEMA(static_cast<double>(TSStatIntGet(dataRateStat)),
                                                   static_cast<double>(dataRateEMA5), static_cast<double>(period * 5 / 1000)));
      ttfbEMA5     = static_cast<int>(calculateEMA(static_cast<double>(TSStatIntGet(ttfbStat)), static_cast<double>(ttfbEMA5),
                                               static_cast<double>(period * 5 / 1000)));
      // Set statistics
      TSStatIntSet(dataRateEma5Stat, dataRateEMA5);
      TSStatIntSet(ttfbEma5Stat, ttfbEMA5);

      // Reset counter
      i = 0;
    }

    // Check for data rate death cross
    // Get second stats
    bool crossCondition2 = TSStatIntGet(dataRateEma5Stat) > TSStatIntGet(dataRateEma1Stat);
    if (crossCondition1 && crossCondition2 && (TSStatIntGet(connectionsToOSRateStat) > 0)) {
      TSDebug(PLUGIN_NAME, "Data Rate Death Cross");
      if (logIsWritable) {
        TSTextLogObjectWrite(*logObj, "Data Rate Death Cross");
        TSTextLogObjectFlush(*logObj);
      }
      // Alert or flag...
    }
  }
}

// Print statistics to log file every 5000 miliseconds
void
printStats()
{
  int period = 5000;
  while (running) {
    // Print to TextLogObject
    if (TSStatIntGet(dataRateStat) != 0) {
      if (logIsWritable) {
        TSTextLogObjectWrite(*logObj, "%llu %llu %llu %llu %llu %llu %llu %llu", TSStatIntGet(dataRateStat),
                             TSStatIntGet(connectionsToOSStat), TSStatIntGet(ttfbStat), TSStatIntGet(dataRateEma1Stat),
                             TSStatIntGet(dataRateEma5Stat), TSStatIntGet(ttfbEma1Stat), TSStatIntGet(ttfbEma5Stat),
                             TSStatIntGet(connectionsToOSRateStat));
        TSTextLogObjectFlush(*logObj);
      }
    }
    // Debug print
    TSDebug(PLUGIN_NAME, "%llu %llu %llu %llu %llu %llu %llu %llu", TSStatIntGet(dataRateStat), TSStatIntGet(connectionsToOSStat),
            TSStatIntGet(ttfbStat), TSStatIntGet(dataRateEma1Stat), TSStatIntGet(dataRateEma5Stat), TSStatIntGet(ttfbEma1Stat),
            TSStatIntGet(ttfbEma5Stat), TSStatIntGet(connectionsToOSRateStat));
    this_thread::sleep_for(std::chrono::milliseconds(period));
  }
}

// EMA = price(t) * k + EMA(y) * (1 - k)
// k = 2/(N + 1)
double
calculateEMA(double current, double oldEMA, double period)
{
  double k   = 2 / (1 + period);
  double ema = current * k + oldEMA * (1 - k);
  return ema;
}