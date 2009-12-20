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
      <p>&nbsp;&nbsp;SSL Termination Proxy-to-Server Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.client.verify.server>Certificate Verification</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.ssl.client.verify.server" value="1" <@checked proxy.config.ssl.client.verify.server\1>>
        Enabled<br>
      <input type="radio" name="proxy.config.ssl.client.verify.server" value="0" <@checked proxy.config.ssl.client.verify.server\0>>
        Disabled<br>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Configures <@record proxy.config.product_name> to verify
        the origin server certificate with the Certificate Authority.
        (CA)
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.client.cert.filename>Client Certificate File</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.client.cert.filename" value="<@record proxy.config.ssl.client.cert.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of the SSL client certificate
            installed on <@record proxy.config.product_name>.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.client.private_key.filename>Client Private Key</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.client.private_key.filename" value="<@record proxy.config.ssl.client.private_key.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of <@record proxy.config.product_name>'s
            private key.  Change this variable only if the private key is not
	    located in the SSL client certificate file.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.client.CA.cert.filename>Certificate Authority</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.client.CA.cert.filename" value="<@record proxy.config.ssl.client.CA.cert.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of the certificate authority
            against which the origin server will be verified.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>
<@include /include/body_footer.ink>

</form>

<@include /include/html_footer.ink>
