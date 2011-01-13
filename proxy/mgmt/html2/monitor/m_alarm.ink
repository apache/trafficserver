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

<!-------------------------------------------------------------------------
  html_header.ink [BUG 53753: special refresh for alarms]
  ------------------------------------------------------------------------->

<html>

<head>
  <title><@record proxy.config.manager_name> UI</title>
  <meta http-equiv="refresh" content="<@record proxy.config.admin.ui_refresh_rate>;URL=/monitor/m_alarm.ink?<@link_query>">
  <meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
  <script language="JavaScript1.2">
    document.write("<link rel='stylesheet' type='text/css' href='/inktomi.css' title='Default'>");
  </script>
</head>

<script language="JavaScript1.2" src="/resize.js" type="text/javascript"></script>

<@include /include/body_header.ink>
<@include /monitor/m_header.ink>

<form method="post" action="/submit_alarm.cgi?<@link_query>">

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;<@record proxy.config.product_name> Alarms</p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="alarmColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="alarmButton" type=submit name="clear" value="  Clear  ">
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left" colspan=2>
      Current Time: <@time>
    </td>
  </tr>
  <tr>
    <td align=middle colspan=2>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr>
          <td class="configureLabel" align="center">Node</td>
          <td class="configureLabel" width="100%" align="center">Alarm</td>
          <td class="configureLabel" align="center">Clear</td>
        </tr>
<@alarm_object>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="alarmColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="alarmButton" type=submit name="clear" value="  Clear  ">
    </td>
  </tr>
</table>

<@include /monitor/m_footer.ink>

</form>

<@include /include/footer.ink>
