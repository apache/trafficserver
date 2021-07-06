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

import java.util.*;
import java.io.*;
import java.net.*;

class ProtocolClient
{
  protected String host;
  protected String proxy;
  protected int port;

  // Constructor
  public ProtocolClient(String host, String proxy, int port)
  {
    this.host = host;
    this.proxy = proxy;
    this.port = port;
  }// End of constructor

  // Method connect()
  public void connect(String fileName)
  {
    try
      {
	Socket client = new Socket(proxy, port);

	// Create output stream and input stream
	BufferedInputStream inStream = new BufferedInputStream(client.getInputStream());
	BufferedOutputStream outStream = new BufferedOutputStream(client.getOutputStream());

	// send out request
	byte[] request = marshalRequest (fileName);
	outStream.write (request);
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

  protected byte[] marshalRequest (String fileName) throws Exception
  {
    String requestStr = new String();
    requestStr = requestStr.concat (host);
    requestStr = requestStr.concat (" ");
    requestStr = requestStr.concat (fileName);
    requestStr = requestStr.concat (" \r\n\r\n");

    System.out.println (requestStr);

    byte[] request = requestStr.getBytes();
    return request;
  }

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
// Test of ProtocolClient
//*******************************************************

public class ProtocolClientTest
{
  public static void main(String[] args)
  {
    // default setting
    String host = "localhost";
    String proxy = "localhost";
    String fileName = null;
    int port = 4665;
    int loop = 1;
    int pipeline = 1;


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
	if (args[i].equals("-s") == true && (i + 1) < args.length)
	  {
	    host = args[i + 1];
	    i ++;
	  }
	if (args[i].equals("-f") == true && (i + 1) < args.length)
	  {
	    fileName = args[i + 1];
	    i ++;
	  }
	if (args[i].equals("-c") == true && (i + 1) < args.length)
	  {
	    pipeline = Integer.parseInt (args[i + 1]);
	    i ++;
	  }
	if (args[i].equals("-l") == true && (i + 1) < args.length)
	  {
	    loop = Integer.parseInt (args[i + 1]);
	    i ++;
	  }
      }


    Vector fileVector = new Vector ();
    File sourceFile = new File (fileName);

    if (!sourceFile.canRead ())
      {
	System.out.println("Usage: input file doesn't exist");
      }
    else
      {
	try
	  {
	    LineNumberReader sourceFileReader =
	      new LineNumberReader (new FileReader (sourceFile));

	    // Read fileNames from the file.
	    String currentFileName;

	    while ((currentFileName = sourceFileReader.readLine()) != null)
		fileVector.addElement (currentFileName);
	  }
	catch (IOException ioe)
	  {
	    ioe.printStackTrace();
	  }
	catch (Exception e)
	  {
	    e.printStackTrace();
	  }
      }

    // Call ProtocolClient and make connection to server through socket.
    for (int i = 0; i < loop; i++)
      {
	for (int j = 0; j < fileVector.size(); j++)
	  {
	    ProtocolClient protocolClient = new ProtocolClient(host, proxy, port);
	    protocolClient.connect(fileVector.elementAt(j).toString());
	  }
      }
  } // End of main function
}// End of class
