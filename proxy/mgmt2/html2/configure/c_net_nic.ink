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

<form method=POST action="/submit_net_config.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;NIC Configurations</p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onClick='return confirm("You are trying to change the network\nparameters. By doing so, you may lose\nthe network connection to this server!");'>
    </td>
    <td>
      <input class="configureButton" type=submit name="cancel" value=" Cancel ">
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr class="warningColor">
    <td height="30">
      <span class="blueLinks">
        Note: If an interface is setup through DHCP, please reconfig it!
      </span>
     </td>
   </tr>
</table>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td colspan="2">
      <@network_object configure>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit name="apply" value="  Apply  " onClick='return confirm("You are trying to change the network\nparameters. By doing so, you may lose\nthe network connection to this server!");'>
    </td>
    <td>
      <input class="configureButton" type=submit name="cancel" value=" Cancel ">
    </td>
  </tr>
</table>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
