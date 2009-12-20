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
      <p>&nbsp;&nbsp;SSL Accelerator Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.accelerator.type>SSL Accelerator Type</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall">None - Software Algorithms</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ssl.accelerator.type" value="0" <@checked proxy.config.ssl.accelerator.type\0>>
                    Selected <br>
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Accelerates SSL with software algorithms. 
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
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall">NCipher Nfast Accelerator Card</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ssl.accelerator.type" value="1" <@checked proxy.config.ssl.accelerator.type\1>>
                    Selected <br>
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Accelerates SSL with NCipher Nfast accelerator card.
                  </ul>
                </td>
              </tr>
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ssl.ncipher.lib.path>Library Path</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.ssl.ncipher.lib.path" value="<@record proxy.config.ssl.ncipher.lib.path>">
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the library path of the NCipher Nfast accelerator card.
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
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall">Rainbow Crypto Swift Accelerator Card</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ssl.accelerator.type" value="2" <@checked proxy.config.ssl.accelerator.type\2>>
                    Selected <br>
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Accelerates SSL with Rainbow Crypto Swift accelerator card.
                  </ul>
                </td>
              </tr>
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ssl.cswift.lib.path>Library Path</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.ssl.cswift.lib.path" value="<@record proxy.config.ssl.cswift.lib.path>">
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the library path of the Rainbow Crypto Swift accelerator card.
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
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall">Compaq Atalla Accelerator Card</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="radio" name="proxy.config.ssl.accelerator.type" value="3" <@checked proxy.config.ssl.accelerator.type\3>>
                    Selected <br>
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Accelerates SSL with Compaq Atalla accelerator card.
                  </ul>
                </td>
              </tr>
              <tr>        
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg proxy.config.ssl.atalla.lib.path>Library Path</td>
              </tr>
              <tr> 
                <td nowrap class="bodyText">
                  <input type="text" size="18" name="proxy.config.ssl.atalla.lib.path" value="<@record proxy.config.ssl.atalla.lib.path>">
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specifies the library path of the Rainbow Crypto Swift accelerator card.
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
<@include /include/body_footer.ink>

</form>

<@include /include/html_footer.ink>
