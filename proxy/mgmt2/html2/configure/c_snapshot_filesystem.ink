<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_snapshot_filesystem.cgi?<@link_query c_snapshot_filesystem>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file c_snapshot_filesystem>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Configuration Snapshots</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

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
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.snapshot_dir>Change Snapshot Directory</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <!--<input type="text" size="22" name="Change Directory" value="<@post_data Change Directory>">-->
      <input type="text" size="22" name="Change Directory" value="<@record proxy.config.snapshot_dir>">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Change the directory where <@record proxy.config.product_name>'s snapshots are stored. Relative
            paths are from the "config" directory
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Snapshots</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="5" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall"><@submit_error_flg SnapShotName>Save Snapshot</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="text" size="22" name="SnapshotName" value="<@post_data SnapshotName>">
                </td>
                <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Specify a name for the new snapshot.
                  </ul>
                </td>
              </tr>

              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall">Restore/Delete Snapshot</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <select name="restore_delete_name" size="1">
                    <option>- select a snapshot -</option>
                    <@select snapshot>
                  </select>
                </td>
                 <td class="configureHelp" valign="top" align="left"> 
                  <ul>
                    <li>Select the snapshot to restore or delete.
                  </ul>
                </td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                  <input type="checkbox" name="Restore Snapshot" value="Restore Snapshot">
                    Restore Snapshot from "<@record proxy.config.snapshot_dir>" Directory<br>
                  <input type="checkbox" name="Delete Snapshot" value="Delete Snapshot">
                    Delete Snapshot from "<@record proxy.config.snapshot_dir>" Directory<br>
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
