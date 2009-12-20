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
      <p>&nbsp;&nbsp;Cache Statistics</p>
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
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Used</td>
          <td class="bodyText"><@record proxy.process.cache.bytes_used\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Cache Size</td>
          <td class="bodyText"><@record proxy.process.cache.bytes_total\b></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Ram Cache</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Bytes Used</td>
          <td class="bodyText"><@record proxy.process.cache.ram_cache.bytes_used\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Total Bytes Available</td>
          <td class="bodyText"><@record proxy.process.cache.ram_cache.total_bytes\b></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Hits</td>
          <td class="bodyText"><@record proxy.process.cache.ram_cache.hits\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Misses</td>
          <td class="bodyText"><@record proxy.process.cache.ram_cache.misses\c></td>
        </tr>
	<!--------------------------------------------------------------------- 
	variables don't need to be displayed in the UI	
	<tr>
          <td height="2" colspan="2" class="configureLabel">Lookups</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;In Progress</td>
          <td class="bodyText"><@record proxy.process.cache.lookup.active\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Hits</td>
          <td class="bodyText"><@record proxy.process.cache.lookup.success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Misses</td>
          <td class="bodyText"><@record proxy.process.cache.lookup.failure\c></td>
        </tr>
	<tr>
 
	---------------------------------------------------------------------->
          <td height="2" colspan="2" class="configureLabel">Reads</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;In Progress</td>
          <td class="bodyText"><@record proxy.process.cache.read.active\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Hits</td>
          <td class="bodyText"><@record proxy.process.cache.read.success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Misses</td>
          <td class="bodyText"><@record proxy.process.cache.read.failure\c></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Writes</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;In Progress</td>
          <td class="bodyText"><@record proxy.process.cache.write.active\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successes</td>
          <td class="bodyText"><@record proxy.process.cache.write.success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Failures</td>
          <td class="bodyText"><@record proxy.process.cache.write.failure\c></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Updates</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;In Progress</td>
          <td class="bodyText"><@record proxy.process.cache.update.active\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successes</td>
          <td class="bodyText"><@record proxy.process.cache.update.success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Failures</td>
          <td class="bodyText"><@record proxy.process.cache.update.failure\c></td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Removes</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;In Progress</td>
          <td class="bodyText"><@record proxy.process.cache.remove.active\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Successes</td>
          <td class="bodyText"><@record proxy.process.cache.remove.success\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Failures</td>
          <td class="bodyText"><@record proxy.process.cache.remove.failure\c></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

