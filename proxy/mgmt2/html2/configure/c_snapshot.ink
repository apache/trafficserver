<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_snapshot.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Configuration Snapshots</p>
    </td>
  </tr>
</table>

<!-------------------------------------------------------------------------
  blue bar
  ------------------------------------------------------------------------->
<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
  </tr>
</table>

<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10">
<!-------------------------------------------------------------------------
  light blue bar
  ------------------------------------------------------------------------->
  <tr> 
    <td colspan="2" class="helpBg">
      <font class="configureBody">
        Snapshots allow you to save and restore
        <@record proxy.config.product_name> configurations.
	<@record proxy.config.product_name> stores snapshots on the
        node in which they were taken.  However, snapshots are
        restored to all nodes in the cluster.
      </font>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">New Snapshot Name</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input type="text" size="18" name="new_snap">
    </td>
    <td>
      <input class="configureButton" type=submit name="snap_action" value="   Take   ">
    </td>
  </tr>
 
  <tr> 
    <td height="2" colspan="2" class="configureLabel">Restore/Delete a Snapshot</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <select name="snap_name" size="1">
        <option>- select a snapshot -</option>
        <@select snapshot>
      </select>
    </td>

    <td nowrap>
      <input class="configureButton" type=submit name="snap_action" value=" Restore "> 
      <input class="configureButton" type=submit name="snap_action" value="  Delete  "> 
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
  </tr>
</table>
<@include /configure/c_footer.ink>
<@include /include/body_footer.ink>

</form>

<@include /include/html_footer.ink>





