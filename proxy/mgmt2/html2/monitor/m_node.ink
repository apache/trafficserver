<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<script language="JavaScript">
<!--
  function newGraphWindow(winName) {
    window.open("", winName, "width=800,height=410,status,resizable=yes");
  }
// -->	
</script>

<@include /include/header.ink>
<@include /monitor/m_header.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Node Summary for <@record proxy.node.hostname></p>
    </td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td class="bodyText">
<@summary_object>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" width="33%">Attribute</td>
          <td class="monitorLabel" width="33%"><@record proxy.node.hostname></td>
          <td class="monitorLabel" width="33%">Cluster Aggregate</td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabel">Cache</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.cache_hit_ratio,Document_Hit_Rate" target=bw_saved onClick="newGraphWindow('bw_saved')">
              &nbsp;&nbsp;&nbsp;Document Hit Rate *
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.cache_hit_ratio_avg_10s\p> </td>
          <td class="bodyText"> <@record proxy.cluster.cache_hit_ratio_avg_10s\p> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.bandwidth_hit_ratio,Bandwidth_Savings" target=bw_saved onClick="newGraphWindow('bw_saved')">
              &nbsp;&nbsp;&nbsp;Bandwidth Savings *
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.bandwidth_hit_ratio_avg_10s\p> </td>
          <td class="bodyText"> <@record proxy.cluster.bandwidth_hit_ratio_avg_10s\p> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.cache.percent_free,Cache_Percent_Free" target=cache_pfree onClick="newGraphWindow('cache_pfree')">
              &nbsp;&nbsp;&nbsp;Cache Percent Free
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.cache.percent_free\p> </td>
          <td class="bodyText"> <@record proxy.cluster.cache.percent_free\p> </td>
        </tr>
        <tr>
          <td height="2" colspan="3" class="configureLabel">In Progress</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_server_connections,Open_Server_Connections" target=os_con onClick="newGraphWindow('os_con')">
              &nbsp;&nbsp;&nbsp;Open Server Connections
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.current_server_connections\c> </td>
          <td class="bodyText"> <@record proxy.cluster.current_server_connections\c> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_client_connections,Open_Client_Connections" target=ua_con onClick="newGraphWindow('ua_con')">
              &nbsp;&nbsp;&nbsp;Open Client Connections
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.current_client_connections\c> </td>
          <td class="bodyText"> <@record proxy.cluster.current_client_connections\c> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_cache_connections,Cache_Transfers_In_Progress" target=cache_con onClick="newGraphWindow('cache_con')">
              &nbsp;&nbsp;&nbsp;Cache Transfers in Progress
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.current_cache_connections\c> </td>
          <td class="bodyText"> <@record proxy.cluster.current_cache_connections\c> </td>
        </tr>
        <tr>
          <td height="2" colspan="3" class="configureLabel">Network</td>
        </tr>
        <tr align="center">
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.client_throughput_out,Client_Throughput__MBit_Per_Sec" target=tput onClick="newGraphWindow('tput')">
              &nbsp;&nbsp;&nbsp;Client Throughput (Mbit/Sec)
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.client_throughput_out> </td>
          <td class="bodyText"> <@record proxy.cluster.client_throughput_out> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.user_agent_xacts_per_second,Transactions_Per_Second" target=xacts onClick="newGraphWindow('xacts')">
              &nbsp;&nbsp;&nbsp;Transactions per Second
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.user_agent_xacts_per_second> </td>
          <td class="bodyText"> <@record proxy.cluster.user_agent_xacts_per_second> </td>
        </tr>
        <tr>
          <td height="2" colspan="3" class="configureLabel">Name Resolution</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.hostdb.hit_ratio,Host_Database_Hit_Rate" target=hdb_r onClick="newGraphWindow('hdb_r')">
              &nbsp;&nbsp;&nbsp;Host Database Hit Rate *
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.hostdb.hit_ratio_avg_10s\p> </td>
          <td class="bodyText"> <@record proxy.cluster.hostdb.hit_ratio_avg_10s\p> </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.dns.lookups_per_second,DNS_Lookups_Per_Second" target=dns_s onClick="newGraphWindow('dns_s')">
              &nbsp;&nbsp;&nbsp;DNS Lookups per Second
            </a>
          </td>
          <td class="bodyText"> <@record proxy.node.dns.lookups_per_second> </td>
          <td class="bodyText"> <@record proxy.cluster.dns.lookups_per_second> </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td class="bodyText" align="right"><em>* Value represents 10 second average</em></td>
  </tr>
</table>

<@include /monitor/m_blue_bar.ink>

<@include /monitor/m_footer.ink>
<@include /include/footer.ink>

