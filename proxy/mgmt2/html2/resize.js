<!-------------------------------------------------------------------------
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
  ------------------------------------------------------------------------->
/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

function resizeIt() {
  if (saveInnerWidth < window.innerWidth || 
      saveInnerWidth > window.innerWidth || 
      saveInnerHeight > window.innerHeight || 
      saveInnerHeight < window.innerHeight ) 
    {
      window.history.go(0);
    }
}

if ((navigator.appName == "Netscape") &&
    (parseInt(navigator.appVersion) >= 4) &&
    (parseInt(navigator.appVersion) < 5)) {
  if(!window.saveInnerWidth) {
    window.onresize = resizeIt;
    window.saveInnerWidth = window.innerWidth;
    window.saveInnerHeight = window.innerHeight;
  }
}
