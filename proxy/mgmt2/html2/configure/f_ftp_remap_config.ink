/////////////////////////////////////////////////////////////////////
// ftp_remap.config form and rule contains following:
//                              (Input Type Names)
//    Traffic Server IP Address        from_ip
//    Traffic Server Port              from_port
//    FTP IP Address                   to_ip
//    FTP Port                         to_port
// ** NOTE: the input type names must match those created in 
//          writeFtpRemapConfigForm
/////////////////////////////////////////////////////////////////////

// 
// creates a new Rule object; initializes with parameters passed in
//
function Rule(from_ip, from_port, to_ip, to_port) {
  this.from_ip = from_ip;
  this.from_port = from_port;
  this.to_ip = to_ip;
  this.to_port = to_port; 
}

// 
// This function creates a new Rule object for ruleList based
// on values entered in form. 
// NOTE: In Netscape, we cannot access the selected value with: 
// document.form1.rule_type.value. So we have to first get the 
// selected index to get selected option's value
// 
function createRuleFromForm() {
  var from_ip = document.form1.from_ip.value;
  var from_port = document.form1.from_port.value;
  var to_ip = document.form1.to_ip.value;
  var to_port = document.form1.to_port.value;
 
  var rule = new Rule(from_ip, from_port, to_ip, to_port);

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

  text = "Proxy" + eq + rule.from_ip + ":" + rule.from_port + delim + "FTP Server" + eq + rule.to_ip + ":" + rule.to_port;
  	
  return text;
}

//
// A Rule object also has a hidden format which will be used to help convert
// it into an Ele when user hits "Apply"
// 
function hiddenFormat(rule)
{
  var text = rule.from_ip + "^" + rule.from_port + "^" + rule.to_ip + "^" + rule.to_port + "^"; 
  return text; 
}

// 
// This function updates the selected Rule object with the values 
// entered on the form. 
// 
function updateRule(index) 
{
  ruleList[index].from_ip = document.form1.from_ip.value;
  ruleList[index].from_port = document.form1.from_port.value;
  ruleList[index].to_ip = document.form1.to_ip.value;
  ruleList[index].to_port = document.form1.to_port.value;
}

// 
// This function updates the elements on the form displayed to the 
// user with the values stored in the ruleList; has an optional index arg
//
function updateForm(index)
{ 
  if (ruleList.length == 0) 
	return; 
	
  if (index == -1)	
    index = document.form1.list1.selectedIndex;
 
  document.form1.from_ip.value = ruleList[index].from_ip;	
  document.form1.from_port.value = ruleList[index].from_port;
  document.form1.to_ip.value = ruleList[index].to_ip;
  document.form1.to_port.value = ruleList[index].to_port;
}

// 
// clears all the fields in the form
// 
function clearForm() 
{
  document.form1.from_ip.value = "";
  document.form1.from_port.value = "";
  document.form1.to_ip.value = "";
  document.form1.to_port.value = "";

  document.form1.list1.selectedIndex = -1; 
}

// 
// form validation - put detailed alert messages in this function
//
function validInput()
{
  if (document.form1.to_ip.value == "" || 
      document.form1.to_port.value == "" || 
      document.form1.from_ip.value == "" || 
      document.form1.from_port.value == "") {
	alert("Invalid input. Need to specify all field values.");
	return false;
  } 

  return true;   	
}
