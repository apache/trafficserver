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
      <p>&nbsp;&nbsp;Log Splitting Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.separate_icp_logs>Split ICP Logs</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.separate_icp_logs" value="1" <@checked proxy.config.log2.separate_icp_logs\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.separate_icp_logs" value="0" <@checked proxy.config.log2.separate_icp_logs\0>>
        Disabled <br>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables splitting of ICP logs.
      </ul>
    </td>
  </tr>

<!--
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.separate_nntp_logs>Split NNTP Logs</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.separate_nntp_logs" value="1" <@checked proxy.config.log2.separate_nntp_logs\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.separate_nntp_logs" value="0" <@checked proxy.config.log2.separate_nntp_logs\0>>
        Disabled <br>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables splitting of NNTP logs.
      </ul>
    </td>
  </tr>
-->
<!--
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.separate_mixt_logs>Split Streaming Media Logs</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.separate_mixt_logs" value="1" <@checked proxy.config.log2.separate_mixt_logs\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.separate_mixt_logs" value="0" <@checked proxy.config.log2.separate_mixt_logs\0>>
        Disabled <br>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables splitting of Streaming Media logs.
      </ul>
    </td>
  </tr>
-->

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.separate_host_logs>Split Host Logs</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.separate_host_logs" value="1" <@checked proxy.config.log2.separate_host_logs\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.separate_host_logs" value="0" <@checked proxy.config.log2.separate_host_logs\0>>
        Disabled <br>
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables splitting of host logs.
      </ul>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
