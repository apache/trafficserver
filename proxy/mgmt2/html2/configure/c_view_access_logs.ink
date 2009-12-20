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

<form method="post" action="/log.cgi?<@link_query>">
<input type=hidden name="submit_from_page" value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Access Logs</p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  ">
    </td>
  </tr>
</table>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td height="2" class="configureLabel">Log File</td>
  </tr>
  <tr>
    <td>
      <select name="logfile" size="1">
        <option value='default'>- select a log file -</option>
        <@select_access_logs>
      </select>
    </td>
  </tr>
  <tr>
    <td height="2" class="configureLabel">Action</td>
  </tr>
  <tr>
    <td class=bodyText nowrap>
      <input type=radio name="action" value="view_all" <@action_checked action\view_all>>
        Display the selected log file<br>
      <@submit_error_flg view_last>
      <input type=radio name="action" value="view_last" <@action_checked action\view_last>>
        Display last <input type=text size=6 name="nlines" value="<@post_data nlines>"> lines of the selected file<br>
      <@submit_error_flg view_subset>
      <input type=radio name="action" value="view_subset" <@action_checked action\view_subset>>
        Display lines that match <input type=text size=20 name="substring" value="<@post_data substring>"> in the selected log file<br>
      <input type=radio name="action" value="remove" <@action_checked action\remove>>
        Remove the selected log file <br>
      <input type=radio name="action" value="save" <@action_checked action\save>>
        Save the selected log file in local filesystem<br>
    </td>
  </tr>

<@log_action /configure/helper/log.sh>

</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  ">
    </td>
  </tr>
</table>

<@include /monitor/m_footer.ink>
</form>
<@include /include/footer.ink>

