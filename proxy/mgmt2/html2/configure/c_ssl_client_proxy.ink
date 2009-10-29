<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;SSL Termination Client-to-Proxy Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.client.certification_level>Client Certificate</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.ssl.client.certification_level" value="0" <@checked proxy.config.ssl.client.certification_level\0>>
        Not Required<br>
      <input type="radio" name="proxy.config.ssl.client.certification_level" value="1" <@checked proxy.config.ssl.client.certification_level\1>>
        Optional<br>
      <input type="radio" name="proxy.config.ssl.client.certification_level" value="2" <@checked proxy.config.ssl.client.certification_level\2>>
        Required
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Sets the client certification level.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.server.cert.filename>Server Certificate File</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.server.cert.filename" value="<@record proxy.config.ssl.server.cert.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of <@record proxy.config.product_name>'s 
            SSL certificate.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.server.private_key.filename>Server Private Key</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.server.private_key.filename" value="<@record proxy.config.ssl.server.private_key.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of <@record proxy.config.product_name>'s 
            private key.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.ssl.CA.cert.filename>Certificate Authority</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.ssl.CA.cert.filename" value="<@record proxy.config.ssl.CA.cert.filename>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the file name of the certificate authority that
            client certificates will be verified against.
      </ul>
    </td>
  </tr>

</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.ssl.server.multicert.filename>SSL Multi-Certificate
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.ssl.server.multicert.filename>" file
      allows an SSL certificate and a private key to be tied to a
      specific IP address on a multi-homed machine.
    </td>
  </tr>
  <tr>
    <td align="center">
      <@file_edit proxy.config.ssl.server.multicert.filename>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
