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
      <p>&nbsp;&nbsp;NNTP Parent Servers Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@nntp_config_display>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
      <@submit_error_flg proxy.config.nntp.servers_filename>NNTP Servers
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left"> 
      The "<@record proxy.config.nntp.config_file>" file lets you
      specify the parent NNTP servers and the type of NNTP activity
      you want the <@record proxy.config.product_name> to observe.
    </td>
  </tr>
  <tr>
    <td align="center">
      <@file_edit proxy.config.nntp.config_file>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
