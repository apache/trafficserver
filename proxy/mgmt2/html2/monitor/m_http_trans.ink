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
      <p>&nbsp;&nbsp;HTTP Transaction Based Statistics</p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" width="33%">Transaction Type</td>
          <td class="monitorLabel" width="33%">Frequency</td>
          <td class="monitorLabel" width="33%">Speed (ms)</td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Hits</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Fresh</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.hit_fresh\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.hit_fresh\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Stale Revalidated</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.hit_revalidated\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.hit_revalidated\c></td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Misses</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Now Cached</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.miss_cold\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.miss_cold\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Server No Cache</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.miss_not_cacheable\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.miss_not_cacheable\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Stale Reloaded</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.miss_changed\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.miss_changed\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Client No Cache</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.miss_client_no_cache\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.miss_client_no_cache\c></td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Errors</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Connection Failures</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.connect_failed\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.connect_failed\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Other Errors</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.other\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.other\c></td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Aborted Transactions</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Client Aborts</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.aborts\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.aborts\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Questionable Client Aborts</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.possible_aborts\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.possible_aborts\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Partial Request Hangups</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.early_hangups\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.early_hangups\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Pre-Request Hangups</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.empty_hangups\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.empty_hangups\c></td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Pre-Connect Hangups</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.errors.pre_accept_hangups\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.errors.pre_accept_hangups\c></td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Other Transactions</td>
        </tr>
        <tr align="center"> 
          <td align=left class="bodyText">&nbsp;&nbsp;&nbsp;Unclassified</td>
          <td class="bodyText"><@record proxy.node.http.transaction_frac_avg_10s.other.unclassified\p></td>
          <td class="bodyText"><@record proxy.node.http.transaction_msec_avg_10s.other.unclassified\c></td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

