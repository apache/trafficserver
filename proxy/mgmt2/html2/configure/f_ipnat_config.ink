/////////////////////////////////////////////////////////////////////
// ipnat.config form and rule contains following:
//    Ethernet Interface               intr
//    Traffic Server IP Address        from_ip
//    optional CIDR                    from_cidr
//    Traffic Server Port              from_port
//    FTP IP Address                   to_ip
//    FTP Port                         to_port
//    Connection Type                  conn_type
//    Protocol                         protocol
// ** NOTE: the input type names must match those created in 
//          writeIpnatConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(intr, from_ip, from_cidr, from_port, to_ip, to_port, conn_type, protocol) {
  this.intr = intr; 
  this.from_ip = from_ip;
  this.from_cidr = from_cidr
  this.from_port = from_port;
  this.to_ip = to_ip;
  this.to_port = to_port; 
  this.conn_type = conn_type;
  this.protocol = protocol; 
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var index = document.form1.conn_type.selectedIndex; 
  var conn_type = document.form1.conn_type.options[index].value;
  var intr = document.form1.intr.value;
  var from_ip = document.form1.from_ip.value;
  var from_cidr = document.form1.from_cidr.value; 
  var from_port = document.form1.from_port.value;
  var to_ip = document.form1.to_ip.value;
  var to_port = document.form1.to_port.value;

  index = document.form1.protocol.selectedIndex; 
  var protocol = document.form1.protocol.options[index].value;

  var rule = new Rule(intr, from_ip, from_cidr, from_port, to_ip, to_port, conn_type, protocol);

  return rule;
}

//
// This function displays the Rule object's information in the format
// that's used in the select list.
//
function textFormat(rule, write)
{
  var text = "";
  var delim = "";
  var eq = "=";  
  var space = "";
  if (write == 1) {
     space = "&nbsp;&nbsp;";
  } else {  
     space = "  ";
  }
  delim = space + "," + space;

  if (rule.from_cidr > 0 || rule.from_cidr == "0") {
     text = "Interface" + eq + rule.intr + delim + "Connection Type" + eq + rule.conn_type + delim + "Source" + eq + rule.from_ip + "/" + rule.from_cidr + ":" + rule.from_port + delim + "Destination" + eq + rule.to_ip + ":" + rule.to_port;
  } else { 
     text = "Interface" + eq + rule.intr + delim + "Connection Type" + eq + rule.conn_type + delim + "Source" + eq + rule.from_ip + ":" + rule.from_port + delim + "Destination" + eq + rule.to_ip + ":" + rule.to_port;
  }
   if (rule.protocol != "") text += delim + "Protocol" + eq + rule.protocol; 
 	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var delim = "^";
  var text = rule.intr + delim + rule.from_ip + delim + rule.from_cidr + delim + rule.from_port + delim + rule.to_ip + delim + rule.to_port + delim + rule.conn_type + delim + rule.protocol + delim; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  var sel = document.form1.conn_type.selectedIndex;
  ruleList[index].conn_type = document.form1.conn_type.options[sel].value;
  ruleList[index].intr = document.form1.intr.value;
  ruleList[index].from_ip = document.form1.from_ip.value;
  ruleList[index].from_cidr = document.form1.from_cidr.value; 
  ruleList[index].from_port = document.form1.from_port.value;
  ruleList[index].to_ip = document.form1.to_ip.value;
  ruleList[index].to_port = document.form1.to_port.value;

  sel = document.form1.protocol.selectedIndex;
  ruleList[index].protocol = document.form1.protocol.options[sel].value;	
}

// 
// This function updates the elements on the form displayed to the 
// user with the values stored in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  var i; 

  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;
 
  document.form1.intr.value = ruleList[index].intr; 
  document.form1.from_ip.value = ruleList[index].from_ip;	
  document.form1.from_cidr.value = ruleList[index].from_cidr; 
  document.form1.from_port.value = ruleList[index].from_port;
  document.form1.to_ip.value = ruleList[index].to_ip;
  document.form1.to_port.value = ruleList[index].to_port;

  for (var i=0; i < document.form1.conn_type.length; i++) {
    if (document.form1.conn_type.options[i].value == ruleList[index].conn_type)
       document.form1.conn_type.selectedIndex = i;
  }

  for (var i=0; i < document.form1.protocol.length; i++) {
    if (document.form1.protocol.options[i].value == ruleList[index].protocol)
       document.form1.protocol.selectedIndex = i;
  }	
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.intr.value = ""; 
  document.form1.from_ip.value = "";
  document.form1.from_cidr.value = ""; 
  document.form1.from_port.value = "";
  document.form1.to_ip.value = "";
  document.form1.to_port.value = "";
  document.form1.conn_type.value = "tcp";
  document.form1.protocol.value = "";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  var conn_index = document.form1.conn_type.selectedIndex;

  if (document.form1.conn_type.options[conn_index].value == "" ||
      document.form1.intr.value == "" ||
      document.form1.to_ip.value == "" || 
      document.form1.to_port.value == "" || 
      document.form1.from_ip.value == "" || 
      document.form1.from_port.value == "") {
	alert("Need to specify all required field values.");
	return false;
  } 

	
  if (document.form1.from_cidr.value != "") { 
     if (document.form1.from_cidr.value > 32 || document.form1.from_cidr.value < 1) {
        if (document.form1.from_cidr.value == 0) {
	  if (document.form1.from_ip.value == "0.0.0.0") {
	      return true;  
          } else {
	   alert("'Source CIDR' can be 0 only if 'Source IP = 0.0.0.0'"); 
	   return false;
          }
        } else {
	   alert("'Source CIDR' must be between 1 and 32");
	   return false;
        }
     }
  }

  return true;   	
}
