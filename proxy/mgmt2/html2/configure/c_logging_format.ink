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

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Log Format Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Squid Format</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.squid_log_enabled>Enable/Disable</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.squid_log_enabled" value="1" <@checked proxy.config.log2.squid_log_enabled\1>>
              Enabled <br>
            <input type="radio" name="proxy.config.log2.squid_log_enabled" value="0" <@checked proxy.config.log2.squid_log_enabled\0>>
              Disabled
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Enables/Disables the generation of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.squid_log_is_ascii>ASCII/Binary</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.squid_log_is_ascii" value="1" <@checked proxy.config.log2.squid_log_is_ascii\1>>
              ASCII <br>
            <input type="radio" name="proxy.config.log2.squid_log_is_ascii" value="0" <@checked proxy.config.log2.squid_log_is_ascii\0>>
              Binary
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies whether to write an ASCII or a binary log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.squid_log_name>Filename</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="18" name="proxy.config.log2.squid_log_name" value="<@record proxy.config.log2.squid_log_name>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the filename of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.squid_log_header>Header</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="36" name="proxy.config.log2.squid_log_header" value="<@record proxy.config.log2.squid_log_header>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the header to write at the beginning of the log.
            </ul>
          </td>
        </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Netscape Common Format</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.common_log_enabled>Enable/Disable</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.common_log_enabled" value="1" <@checked proxy.config.log2.common_log_enabled\1>>
              Enabled <br>
            <input type="radio" name="proxy.config.log2.common_log_enabled" value="0" <@checked proxy.config.log2.common_log_enabled\0>>
              Disabled
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Enables/Disables the generation of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.common_log_is_ascii>ASCII/Binary</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.common_log_is_ascii" value="1" <@checked proxy.config.log2.common_log_is_ascii\1>>
              ASCII <br>
            <input type="radio" name="proxy.config.log2.common_log_is_ascii" value="0" <@checked proxy.config.log2.common_log_is_ascii\0>>
              Binary
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies whether to write an ASCII or a binary log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.common_log_name>Filename</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="18" name="proxy.config.log2.common_log_name" value="<@record proxy.config.log2.common_log_name>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the filename of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.common_log_header>Header</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="36" name="proxy.config.log2.common_log_header" value="<@record proxy.config.log2.common_log_header>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the header to write at the beginning of the log.
            </ul>
          </td>
        </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Netscape Extended Format</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended_log_enabled>Enable/Disable</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.extended_log_enabled" value="1" <@checked proxy.config.log2.extended_log_enabled\1>>
              Enabled <br>
            <input type="radio" name="proxy.config.log2.extended_log_enabled" value="0" <@checked proxy.config.log2.extended_log_enabled\0>>
              Disabled
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Enables/Disables the generation of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended_log_is_ascii>ASCII/Binary</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.extended_log_is_ascii" value="1" <@checked proxy.config.log2.extended_log_is_ascii\1>>
              ASCII <br>
            <input type="radio" name="proxy.config.log2.extended_log_is_ascii" value="0" <@checked proxy.config.log2.extended_log_is_ascii\0>>
              Binary
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies whether to write an ASCII or a binary log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended_log_name>Filename</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="18" name="proxy.config.log2.extended_log_name" value="<@record proxy.config.log2.extended_log_name>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the filename of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended_log_header>Header</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="36" name="proxy.config.log2.extended_log_header" value="<@record proxy.config.log2.extended_log_header>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the header to write at the beginning of the log.
            </ul>
          </td>
        </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Netscape Extended 2 Format</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended2_log_enabled>Enable/Disable</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.extended2_log_enabled" value="1" <@checked proxy.config.log2.extended2_log_enabled\1>>
              Enabled <br>
            <input type="radio" name="proxy.config.log2.extended2_log_enabled" value="0" <@checked proxy.config.log2.extended2_log_enabled\0>>
              Disabled
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Enables/Disables the generation of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended2_log_is_ascii>ASCII/Binary</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="radio" name="proxy.config.log2.extended2_log_is_ascii" value="1" <@checked proxy.config.log2.extended2_log_is_ascii\1>>
              ASCII <br>
            <input type="radio" name="proxy.config.log2.extended2_log_is_ascii" value="0" <@checked proxy.config.log2.extended2_log_is_ascii\0>>
              Binary
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies whether to write an ASCII or a binary log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended2_log_name>Filename</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="18" name="proxy.config.log2.extended2_log_name" value="<@record proxy.config.log2.extended2_log_name>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the filename of the log.
            </ul>
          </td>
        </tr>
        <tr> 
          <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.log2.extended2_log_header>Header</td>
        </tr>
        <tr>
          <td nowrap class="bodyText">
            <input type="text" size="36" name="proxy.config.log2.extended2_log_header" value="<@record proxy.config.log2.extended2_log_header>">
          </td>
          <td class="configureHelp" valign="top" align="left"> 
            <ul>
              <li>Specifies the header to write at the beginning of the log.
            </ul>
          </td>
        </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
