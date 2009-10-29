The null-transform.c plugin performs a null transformation
on response content. 

The plugin is called each time Traffic Server reads an HTTP 
response header.

  --  The INKPluginInit function has a global hook
      set up for the READ RESPONSE HDR event.

This plugin follows the default behavior for transforms: 
transformed content is cached. Therefore the transformation 
of the response need happen only once, when the content is
received from the origin server. To make sure this happens,
the plugin checks for a "200 OK" server response header
before transforming. 

  -- This check is done in the subroutine called "transformable". 

If the response is transformable, the plugin creates a 
transformation, and adds it to the response transform
hook. 

  -- This is done in "transform_add" using 
     INKTransformCreate and INKHttpTxnHookAdd.

  -- The handler function for the transformation is
     null_transform.

When it is time to transform the response data, the 
null_transform function is called. 

The transformation acts as a one-way data pipe: response
data comes in from an upstream vconnection (which could be
an HTTP state machine or itself another transformation) and
goes out to the downstream vconnection, whatever that might
be. The transformation has to:

(a)  Write transformed data to the downstream vconnection. 

(b)  Copy data from the upstream vconnection's output buffer
     to the downstream vconnection's input buffer. 

(c)  Clean up when the transformation is complete (either if
     it stopped because of an error or it finished transforming).

Here is how this is implemented: the null_transform function
(the transformation's handler function) first checks to make
sure the transformation has not been closed (null_transform
destroys itself if it finds out the transformation has been 
closed). Then null_transform has a switch statement that 
handles the following events: 

  -- INK_EVENT_ERROR: if there is an error, null_transform
     lets the downstream vconnection know that the write
     operation is terminated (the downstream vconnection
     should not expect any more data). 

  -- INK_EVENT_VCONN_WRITE_COMPLETE: the downstream vconnection
     has read all the data written to it. null_transform 
     shuts down the write portion of the downstream vconnection,
     meaning that the transformation does not want any more 
     WRITE events from the downstream vconnection. 

  -- INK_EVENT_VCONN_WRITE_READY: null_transform calls 
     handle_transform to transform data.

  -- All other events: call handle_transform. 

In the handle_transform function, the transformation vconnection
takes the role of the "vconnection user" (see the SDK Programmer's
Guide).  

handle_transform needs to initiate the transformation by a call
to INKVConnWrite on the output vconnection. To do this, handle_transform
has to:

 -- get the output vconnection using INKTransformOutputVConnGet

 -- get the input vio using INKVConnWriteVIOGet (the input vio
    contains the total number of bytes to be written, and keeps
    track of the number of bytes that the upstream vconnection
    has written to the input buffer. When the transformation has
    consumed data from the input buffer, it has to modify the 
    input vio.)

After calling INKVConnWrite, the transformation can expect to 
receive WRITE_READY and WRITE_COMPLETE events from the downstream
vconnection. 

If there is data to read, handle_transform copies it over using
INKIOBufferCopy. When done with the buffer, it calls
INKIOBufferReaderConsume. If there is more data to read (than one
buffer's worth), two things happen:

 -- handle_transform wakes up the downstream vconnection using
    INKVIOReenable

 -- handle_transform wakes up the upstream vconnection, asking it
    for more data, by using INKContCall and sending it a 
    WRITE_READY event

If there is no more data to read, handle_transform informs the 
downstream vconnection using INKVIONBytesSet and INKVIOReenable.
Then handle_transform sends the upstream vconnection the
WRITE_COMPLETE event using INKContCall. 

This is how the transformation receives the WRITE_COMPLETE event:
when the downstream vconnection learns through the downstream
(output) vio that there is no more data left to read (nbytes=ndone),
the downstream vconnection sends WRITE_COMPLETE upstream.  


