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

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<@overview_details_object>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">

        <tr> 
          <td align="center" class="monitorLabelSmall">Node</td>
          <td align="center" class="monitorLabelSmall">On/Off</td>
          <td align="center" class="monitorLabelSmall">Objects Served</td>
          <td align="center" class="monitorLabelSmall">Ops/Sec</td>
          <td align="center" class="monitorLabelSmall">Hit Rate</td>
          <td align="center" class="monitorLabelSmall">Throughput (Mbit/sec)</td>
          <td align="center" class="monitorLabelSmall">HTTP Hit (ms)</td>
          <td align="center" class="monitorLabelSmall">HTTP Miss (ms)</td>
        </tr>
<@overview_object>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="secondaryColor">
    <td class="secondaryColor" width="2" height="10"><img src="/images/dot_clear.gif" width="2" height="10"></td>
  </tr>
</table>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
