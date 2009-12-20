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

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Real Networks Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" colspan="2">Client</td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">On Demand</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText" width="66%">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText" width="33%"><@record proxy.process.rni.current_client_connections\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.rni.downstream_requests\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.rni.downstream.response_bytes\b></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Live</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText"><@record proxy.process.rni.current_live_streams\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.rni.total_live_streams\c></td>
        </tr>
      </table>
    </td>
  </tr>

  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" colspan="2">Server</td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">On Demand</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText" width="66%">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText" width="33%"><@record proxy.process.rni.upstream.response_bytes\b></td>
        </tr>
      </table>
    </td>
  </tr>

  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" colspan="2">Cache</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText" width="66%">&nbsp;&nbsp;&nbsp;Total Bytes Hit</td>
          <td class="bodyText" width="33%"><@record proxy.process.rni.byte_hit_sum\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Bytes Missed</td>
          <td class="bodyText"><@record proxy.process.rni.byte_miss_sum\b></td>
        </tr>
      </table>
    </td>
  </tr>

</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
