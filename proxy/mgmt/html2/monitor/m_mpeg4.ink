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
      <p>&nbsp;&nbsp;MPEG4 Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" width="66%">Attribute</td>
          <td class="monitorLabel" width="33%">Current Value</td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">General</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Objects Served</td>
          <td class="bodyText"><@record proxy.process.mpeg4.object_count\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Block Hits</td>
          <td class="bodyText"><@record proxy.process.mpeg4.block_hit_count\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Block Misses</td>
          <td class="bodyText"><@record proxy.process.mpeg4.block_miss_count\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Bytes Hit</td>
          <td class="bodyText"><@record proxy.process.mpeg4.byte_hit_count\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Bytes Missed</td>
          <td class="bodyText"><@record proxy.process.mpeg4.byte_miss_count\b></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Live Streams</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Current Live Streams</td>
          <td class="bodyText"><@record proxy.process.mpeg4.current_unique_live_streams\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Live Streams</td>
          <td class="bodyText"><@record proxy.process.mpeg4.unique_live_streams\c></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Client</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText"><@record proxy.process.mpeg4.current_client_connections\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.mpeg4.downstream_requests\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Request Bytes</td>
          <td class="bodyText"><@record proxy.process.mpeg4.downstream.request_bytes\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.mpeg4.downstream.response_bytes\b></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Server</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Connections</td>
          <td class="bodyText"><@record proxy.process.mpeg4.current_server_connections\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Number of Requests</td>
          <td class="bodyText"><@record proxy.process.mpeg4.upstream_requests\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Request Bytes</td>
          <td class="bodyText"><@record proxy.process.mpeg4.upstream.request_bytes\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Response Bytes</td>
          <td class="bodyText"><@record proxy.process.mpeg4.upstream.response_bytes\b></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>
