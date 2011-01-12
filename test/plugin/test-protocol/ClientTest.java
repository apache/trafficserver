//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

/**************************************************************************
 * This is the java client that sends requests to the test-protocol plugin
 * and gets back the response.
 **************************************************************************/
import java.util.*;
import java.io.*;
import java.net.*;

class TestProtocolClient
{
  protected String proxy;
  protected int port;
  protected String pattern;
  protected String number;
  protected String fileName;
  
  // Constructor
  public TestProtocolClient(String proxy, int port, String pattern, String number)
  {
    this.proxy = proxy;
    this.port = port;
    this.pattern = pattern;
    this.number = number;
    this.fileName = fileName;
  }// End of constructor
  
  // Method connect()
  public void connect()
  {
    try
      {
	Socket client = new Socket(proxy, port);
	
	// Create output stream and input stream
	BufferedInputStream inStream = new BufferedInputStream(client.getInputStream());
	PrintWriter outStream = new PrintWriter(client.getOutputStream());
	
	// send out request
        outStream.print(pattern + " " + number + "\r\n\r\n");
        outStream.flush ();

	// Print out response, actually pattern mapping can be done here
	// to verify the response is correct.
	String responseBody = unmarshalResponse (inStream);
	System.out.println(responseBody);
	
	// Close socket
	client.close();
      } catch (UnknownHostException uhe) {
	//System.out.println("Unknown host " + host);
	uhe.printStackTrace();
      } catch (IOException ioe) {
	//System.out.println("IOException " + ioe);
	ioe.printStackTrace();
      } catch (Exception e) {
	e.printStackTrace();
      }
      
  }// End of connect()

  protected String unmarshalResponse (BufferedInputStream responseStream) throws Exception
  {
    byte[] newObject = new byte[0];
    byte[] oldObject = new byte[0];
    byte[] buffer = new byte[1024];
    int bytesRead = 0;
    while ((bytesRead = responseStream.read(buffer)) != -1)
      {
	oldObject = newObject;
	newObject = new byte[oldObject.length + bytesRead];
	for (int i = 0; i < oldObject.length; i++)
	  newObject[i] = oldObject[i];
	for (int i = 0; i < bytesRead; i++)
	  newObject[oldObject.length + i] = buffer[i];
      }
    
    String responseBody = new String (newObject);
    return responseBody;
  }

}// End of class

//*******************************************************
// Test of TestProtocolClient 
//*******************************************************

public class ClientTest
{
  public static void main(String[] args)
  {
    // default setting
    String proxy = "ts-sun22.tsqa.example.com";
    int port = 7493;
    String pattern = "abc";
    String number  = "3";
    int loop = 1;

    if(args.length == 0) {
        System.out.println("Usage: java ClientTest -P <proxy_name> -p <proxy_port> -a <pattern> -n <number> -l <loop>");
    }

    for (int i = 0; i < args.length; i++)
      {
	if (args[i].equals("-P") == true && (i + 1) < args.length)
	  {
	    proxy = args[i + 1];
	    i ++;
	  }
	if (args[i].equals("-p") == true && (i + 1) < args.length)
	  {
	    port = Integer.parseInt (args[i + 1]);
	    i ++;
	  }
	if (args[i].equals("-a") == true && (i + 1) < args.length)
	  {
	    pattern = args[i + 1];
	    i ++;
	  }
	if (args[i].equals("-n") == true && (i + 1) < args.length)
	  {
	    number = args[i + 1];
	    i ++;
	  }
	if (args[i].equals("-l") == true && (i + 1) < args.length)
	  {
	    loop = Integer.parseInt (args[i + 1]);
	    i ++;
	  }
      }
    

    // Call TestProtocolClient and make connection to server through socket.
    if(loop == -1){
	while(true) {
          TestProtocolClient testProtocolClient = new TestProtocolClient(proxy, port, pattern, number);
	  testProtocolClient.connect();
	}

    } else {

      for (int i = 0; i < loop; i++)
        {
          TestProtocolClient testProtocolClient = new TestProtocolClient(proxy, port, pattern, number);
	  testProtocolClient.connect();
        }

    }
  } // End of main function
}// End of class
