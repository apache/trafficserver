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

<form method=POST action="/charting/chart.cgi" target=multi>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Real-Time Graphs</p>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit value="  Graph  " target=multi onClick="newGraphWindow('multi')">
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="monitorLabel" width="100%">Attribute</td>
          <td class="monitorLabel">Graph</td>
        </tr>
	<tr>
          <td height="2" colspan="2" class="configureLabel">Cache</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.cache_hit_ratio,Document_Hit_Rate" target=hit_ratio onClick="newGraphWindow('hit_ratio')" >
              &nbsp;&nbsp;&nbsp;Document Hit Rate
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Document Hit Rate" VALUE="proxy.node.cache_hit_ratio">
          </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.bandwidth_hit_ratio,Bandwidth_Savings" target=bw_saved onClick="newGraphWindow('bw_saved')" >
              &nbsp;&nbsp;&nbsp;Bandwidth Savings
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Bandwidth Savings" VALUE="proxy.node.bandwidth_hit_ratio">
          </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.cache.percent_free,Cache_Percent_Free" target=cache_pfree onClick="newGraphWindow('cache_pfree')">
              &nbsp;&nbsp;&nbsp;Cache Percent Free
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Cache Percent Free" VALUE="proxy.node.cache.percent_free">
          </td>
        </tr>

	<tr>
          <td height="2" colspan="2" class="configureLabel">In Progress</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_server_connections,Open_Server_Connections" target=os_con onClick="newGraphWindow('os_con')">
              &nbsp;&nbsp;&nbsp;Open Server Connections
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Open Server Connections" VALUE="proxy.node.current_server_connections">
          </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_client_connections,Open_Client_Connections" target=ua_con onClick="newGraphWindow('ua_con')">
              &nbsp;&nbsp;&nbsp;Open Client Connections
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Open Client Connections" VALUE="proxy.node.current_client_connections">
          </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.current_cache_connections,Cache_Transfers_In_Progress" target=cache_con onClick="newGraphWindow('cache_con')">
              &nbsp;&nbsp;&nbsp;Cache Transfers In Progress
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Cache Transfers In Progress" VALUE="proxy.node.current_cache_connections">
          </td>
        </tr>

	<tr>
          <td height="2" colspan="2" class="configureLabel">Network</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.client_throughput_out,Client_Throughput" target=tput onClick="newGraphWindow('tput')">
              &nbsp;&nbsp;&nbsp;Client Throughput (MBit/Sec)
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Client Throughput" VALUE="proxy.node.client_throughput_out">
          </td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.user_agent_xacts_per_second,Transactions_Per_Second" target=xacts onClick="newGraphWindow('xacts')">
              &nbsp;&nbsp;&nbsp;Transactions Per Second
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Transactions Per Second" VALUE="proxy.node.user_agent_xacts_per_second">
          </td>
        </tr>

	<tr>
          <td height="2" colspan="2" class="configureLabel">Name Resolution</td>
        </tr>
        <tr align="center"> 
          <td class="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.hostdb.hit_ratio,Host_Database_Hit_Rate" target=hdb_r onClick="newGraphWindow('hdb_r')">
              &nbsp;&nbsp;&nbsp;Host Database Hit Rate
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="Host Database Hit Rate" VALUE="proxy.node.hostdb.hit_ratio">
          </td>
        </tr>
        <tr align="center"> 
          <td clsas="bodyText" align=left>
            <a class="graph" href="/charting/chart.cgi?cluster=proxy.node.dns.lookups_per_second,DNS_Lookups_Per_Second" target=dns_s onClick="newGraphWindow('dns_s')">
              &nbsp;&nbsp;&nbsp;DNS Lookups Per Second
            </a>
          </td>
          <td>
            <INPUT TYPE="checkbox" NAME="DNS Lookups Per Second" VALUE="proxy.node.dns.lookups_per_second">
          </td>
        </tr>
      </table>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="3">
  <tr class="secondaryColor">
    <td width="100%" nowrap>
      &nbsp;
    </td>
    <td>
      <input class="configureButton" type=submit value="  Graph  " target=multi onClick="newGraphWindow('multi')">
    </td>
  </tr>
</table>

<@include /monitor/m_footer.ink>

</form>

<@include /include/footer.ink>
