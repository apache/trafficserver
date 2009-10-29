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
      <p>&nbsp;&nbsp;Log Collation Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.local.log2.collation_mode><@submit_error_flg proxy.config.log2.collation_host>Collation Mode</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.local.log2.collation_mode" value="0" <@checked proxy.local.log2.collation_mode\0>>
        Collation Disabled <br>
      <input type="radio" name="proxy.local.log2.collation_mode" value="1" <@checked proxy.local.log2.collation_mode\1>>
        Be a Collation Server <br>
      <input type="radio" name="proxy.local.log2.collation_mode" value="2" <@checked proxy.local.log2.collation_mode\2>>
        Be a Collation Client <br>   
    </td>

    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the log collation mode.  When configured as a
        collation client, <@record proxy.config.product_name> will
        only send standard logs to the collation server.  Custom logs
        can be configured to be sent to a collation server in the
        "<@record proxy.config.log2.xml_config_file>" file.<br>
      </ul>
    </td>
  </tr>
   
  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.collation_port>Log Collation Server</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.log2.collation_host" value="<@record proxy.config.log2.collation_host>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the log collation server to which standard logs
        will be sent.  This configuration is used only if the
        Collation Mode is set to "Be a Collation Client".
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.collation_port>Log Collation Port</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.log2.collation_port" value="<@record proxy.config.log2.collation_port>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the port used for communication between the
            log collation server and client.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.collation_secret>Log Collation Secret</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="proxy.config.log2.collation_secret" value="<@record proxy.config.log2.collation_secret>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the password used to validate logging data and
            prevent the exchange of unauthorized information when a
            collation server is being used.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.collation_host_tagged>Log Collation Host Tagged</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText">
      <input type="radio" name="proxy.config.log2.collation_host_tagged" value="1" <@checked proxy.config.log2.collation_host_tagged\1>>
        Enabled <br>
      <input type="radio" name="proxy.config.log2.collation_host_tagged" value="0" <@checked proxy.config.log2.collation_host_tagged\0>>
        Disabled
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>When enabled, configures <@record proxy.config.product_name> 
            to include the hostname of the collation client that generated 
            the log entry in each entry.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.log2.max_space_mb_for_orphan_logs>Log Collation Orphan Space</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="6" name="proxy.config.log2.max_space_mb_for_orphan_logs" value="<@record proxy.config.log2.max_space_mb_for_orphan_logs>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the amount of space allocated to the logging
            directory in MB if this node is acting as a collation client.
      </ul>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
