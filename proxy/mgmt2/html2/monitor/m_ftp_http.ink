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
<@include /configure/c_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;FTP over HTTP Statistics</p>
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
          <td height="2" colspan="2" class="configureLabel">Connections</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Open Server Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.connections_currently_open\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successful PASV Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.connections_successful_pasv></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Failed PASV Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.connections_failed_pasv></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successful PORT Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.connections_successful_port></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Failed PORT Connections</td>
          <td class="bodyText"><@record proxy.process.ftp.connections_failed_port></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Cache Statistics</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Hits</td>
          <td class="bodyText"><@record proxy.process.ftp.cache_hits\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Misses</td>
          <td class="bodyText"><@record proxy.process.ftp.cache_misses\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Lookups</td>
          <td class="bodyText"><@record proxy.process.ftp.cache_lookups\c></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

