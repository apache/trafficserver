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
      <p>&nbsp;&nbsp;NNTP Timeout Configurations</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>


<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.client_timeout>Client Timeout</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.nntp.inactivity_timeout" value="<@record proxy.config.nntp.client_timeout>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the number of seconds that idle client NNTP connections
            can remain open.
        <li>Minimum value is 3 minutes.
      </ul>
    </td>
  </tr>
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.nntp.server_timeout>Server Timeout</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.nntp.inactivity_timeout" value="<@record proxy.config.nntp.server_timeout>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the number of seconds that idle server NNTP connections
            can remain open.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
