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

#include <time.h>
#include <stdint.h>
#include <iostream>
#include <utility>
#include <vector>
#include <map>
#include <sys/time.h>
using namespace std;

static int windowSize    = 200;  // ms
static int totalDuration = 2000; // seconds
static int lowerCutOff   = 300;
static int higherCutOff  = 1000;

class FailureInfo
{
public:
  FailureInfo()
  {
    gettimeofday(&_start, NULL);
    _totalSlot = totalDuration / windowSize; // INtegral multiple
    _marker    = 0;
    for (int i = 0; i < _totalSlot; i++)
      _passFail.push_back(make_pair(0, 0));

    _avgOverWindow = 0;
    _windowPassed  = 0;
  };

  ~FailureInfo() {}
  // Whenever the window time expires u start filling the count
  // by taking a mod
  // so what u get is over a window of 200 ms and 10 rounds
  // the no of failures
  // Introduce a variable which will be a function of
  // failure and which will lead to points in graph
  // according to which the probability of serving the
  // data from cache or contacting the origin server
  // will be decided
  std::vector<std::pair<double, double>> _passFail;

  int _marker;

  int _totalSlot;

  struct timeval _start;

  double _avgOverWindow;

  int _windowPassed;
};

typedef std::map<std::string, class FailureInfo *> FailureData;

void
registerSuccFail(string URL, FailureData &data, bool isSuccess)
{
  struct timeval currTime, result, startTime;
  int marker;
  FailureData::iterator it;
  it                                     = data.find(URL);
  vector<pair<double, double>> &passFail = it->second->_passFail;
  marker                                 = it->second->_marker;

  startTime = it->second->_start;

  gettimeofday(&currTime, NULL);

  timersub(&currTime, &startTime, &result);

  if ((result.tv_sec * 1000000 + result.tv_usec) > (windowSize * 1000)) {
    marker = ++marker % it->second->_totalSlot;
    if (marker == it->second->_totalSlot - 1) {
      ++it->second->_windowPassed;
      double avg = 0;
      for (int i = 0; i < it->second->_totalSlot; i++) {
        if (passFail[i].first > 0) {
          avg += passFail[i].first / (passFail[i].first + passFail[i].second);
        }
      }
      it->second->_avgOverWindow += avg / it->second->_windowPassed;
    }
    it->second->_marker = marker;
    gettimeofday(&it->second->_start, NULL);
  }

  if (isSuccess) {
    passFail[marker].second++;
  }

  else {
    passFail[marker].first++;
  }
}

bool
isAttemptReq(string URL, FailureData &data)
{
  FailureData::iterator it;
  it = data.find(URL);
  if (it != data.end()) {
    double avg                             = 0;
    vector<pair<double, double>> &passFail = it->second->_passFail;

    for (int i = 0; i < it->second->_totalSlot; i++) {
      // cout<<"Failure:"<<passFail[i].first<< "Total"<< (passFail[i].first+passFail[i].second )<<endl;
      if (passFail[i].first > 0) {
        avg += passFail[i].first / (passFail[i].first + passFail[i].second);
        // cout<<"Prob of failure:"<<passFail[i].first/(passFail[i].first+passFail[i].second)<<endl;
      }
    }

    if (avg) {
      avg = avg / it->second->_totalSlot;
      double prob;

      if (avg * 1000 < lowerCutOff) {
        prob = avg;
      }

      else {
        double mapFactor = (((avg * 1000 - lowerCutOff) * (avg * 1000 - lowerCutOff)) / (higherCutOff - lowerCutOff)) + lowerCutOff;
        prob             = mapFactor / 1000;
        cout << prob << endl;
      }

      if (prob == 1) {
        prob = it->second->_avgOverWindow;
        cout << "Average" << prob << endl;
      }

      int decision = rand() % 100;

      if (decision < prob * 100)
        return false;

      return true;
    }
    return true;

  } else {
    FailureInfo *info = new FailureInfo();
    data[URL]         = info;
    return true;
  }
}

const std::string fetchURL = "www.example.com";
int
main(int argc, char **argv)
{
  // Simulate the scenario
  FailureData data;
  int noOfAttempt = 0, noOfExcept = 0;

  int count = atoi(argv[1]);
  while (count--) {
    int decision = rand() % 100;

    if (isAttemptReq(fetchURL, data)) {
      noOfAttempt++;
      if (decision >= atoi(argv[2]) && 0)
        registerSuccFail(fetchURL, data, true);

      else
        registerSuccFail(fetchURL, data, false);
    } else
      noOfExcept++;
  }

  cout << " SERVED FROM ATTEMPT " << noOfAttempt << " TOTAL " << atoi(argv[1]) << endl;
  cout << " SERVED FROM EXCEPT " << noOfExcept << " TOTAL " << atoi(argv[1]) << endl;
}
