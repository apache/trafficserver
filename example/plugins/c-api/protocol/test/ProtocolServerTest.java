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

import java.io.*;
import java.net.*;
import java.util.*;

class ProtocolServer extends Thread
{
  protected ServerSocket listener = null;
  protected static ProtocolServer instance = null;

  public ProtocolServer (int port)
  {
    try
      {
	listener = new ServerSocket(port);
      }
    catch(IOException ioe)
      {
	ioe.printStackTrace();
      }
  }

  public synchronized void run ()
  {
    while (true)
      {
	try
	  {
	    Socket socket = listener.accept();
	    new ProtocolRequestHandler(socket).start();
	  }
	catch (IOException ioe)
	  {
	    ioe.printStackTrace();
	  }
      }
  }

  public static ProtocolServer getInstance (int port)
  {
    if (instance == null)
      {
	instance = new ProtocolServer (port);
      }
    return instance;
  }
}// end of class ProtocolServer

class ProtocolRequestHandler extends Thread
{
  protected Socket socket = null;
  protected String requestBody;

  // constructor
  public ProtocolRequestHandler (Socket socket)
  {
    this.socket = socket;
  }

  public synchronized void run ()
  {
    try
      {
	// unmarshal the request
	System.out.println ("call unmarshalRequest");
	requestBody = unmarshalRequest();

	// marshal the response
	System.out.println ("call marshalResponse");
	byte[] response = marshalResponse (requestBody);

	// send the response
	if (response != null)
	  {
	    BufferedOutputStream responseStream =
	      new BufferedOutputStream(socket.getOutputStream());
	    responseStream.write (response);
	    responseStream.flush ();
	  }
	// close the socket
	System.out.println("close socket");
	socket.close ();
      }
    catch (Exception e)
      {
	e.printStackTrace();
	try
	  {
	    socket.close ();
	  }
	catch (Exception socke)
	  {
	    socke.printStackTrace();
	  }
      }
  }

  protected String unmarshalRequest() throws Exception
  {
    BufferedReader requestReader =
      new BufferedReader (new InputStreamReader(socket.getInputStream()));

    String request = requestReader.readLine();
    requestReader.readLine();
    System.out.println ("request is " + request);

    StringTokenizer tok = new StringTokenizer (request, " ");
    String host = tok.nextToken();
    String fileName = tok.nextToken();

    return fileName;
  }

  protected byte[] marshalResponse(String requestBody) throws Exception
  {
    byte[] responseBody;

    // find the file corresponding to the file name defined in requestBody
    File responseFile = new File (requestBody);
    if (responseFile.canRead())
      {
	FileInputStream responseFileStream =
	  new FileInputStream (responseFile);

	// read data from the file.
	int bytesRead = 0;
	byte[] buffer = new byte[1024];
	responseBody = new byte[0];
	byte[] temp = new byte[0];
 	while ((bytesRead = responseFileStream.read (buffer)) != -1)
	  {
	    temp = responseBody;
	    responseBody = new byte[temp.length + bytesRead];
	    for (int i = 0; i < temp.length; i++)
	      responseBody[i] = temp[i];
	    for (int i = 0; i < bytesRead; i++)
	      responseBody[temp.length + i] = buffer[i];
	  }
      }
    else
      {
	//String errorMessage = new String ("Error reading " + requestBody);
	//responseBody = new byte[1024];
	//responseBody = errorMessage.getBytes();
	return null;
      }
    System.out.println (new String(responseBody));
    return responseBody;
  }
}// end of class ProtocolRequestHandler


public class ProtocolServerTest
{
    public static void main(String[] args)
    {
        int port = 8175;
        if(args.length > 0)
            port = Integer.parseInt(args[0]);

        // Call networkClient and make connection to server through socket.
        ProtocolServer protocolServer = ProtocolServer.getInstance (port);
        protocolServer.start();
    }
}// End of class

