if (4 > process.argv.length) {
    console.error("usage: node origin.js <id> <json config file>");
    process.exit(1);
}

var full_conf = JSON.parse(require('fs').readFileSync(process.argv[3]));
var conf = full_conf.servers[process.argv[2]];

if (!conf) {
    console.error("Config file '" + process.argv[3] + "' is missing section"
            + " for servers::" + process.argv[2]);
    process.exit(1);
}

var server = require('http').createServer(function(request, response) {
    // Send 404 if the method or path doesn't map to an action

    if (!conf.actions || !conf.actions[request.method] ||
            !conf.actions[request.method][request.url]) {
        console.log('404: ' + request.method + ' ' + request.url);
        response.writeHead(404, {});
        response.end();
        return;
    }

    var action = conf.actions[request.method][request.url];

    // Recycle the response chunk for the action

    if (!action.response_chunk) {
        var chunk_size_bytes = action.chunk_size_bytes || 1024;
        var chunk_byte_value = action.chunk_byte_value || 42;
        
        action.response_chunk = new Buffer(chunk_size_bytes);
        action.response_chunk.fill(chunk_byte_value);        
    }

    // Send the configured response

    // Config settings. Default as necessary
    var delay_first_chunk_millis = action.delay_first_chunk_millis || 0;
    var num_chunks = action.num_chunks || 0;
    var delay_between_chunk_millis = action.delay_between_chunk_millis || 0;
    var status_code = action.status_code || 200;
    var headers = action.headers || {};

    // Per-action state.
    var response_chunk = action.response_chunk;

    // Per-transaction state.
    var chunks_sent = 0;

    // Send headers

    response.writeHead(status_code, headers);

    // Send no chunks

    if (chunks_sent >= num_chunks) {
        response.end();
        return;
    }

    // Send n chunks

    var send_chunk = function() {
        ++chunks_sent;

        // Last chunk

        if (chunks_sent >= num_chunks) {
            response.end(response_chunk);
            return;
        }

        // Intermediate chunk

        response.write(response_chunk);

        if (0 < delay_between_chunk_millis) {
            setTimeout(send_chunk, delay_between_chunk_millis);
        } else {
            process.nextTick(send_chunk);
        }
    };

    if (0 < delay_first_chunk_millis) {
        setTimeout(send_chunk, delay_first_chunk_millis);
    } else {
        process.nextTick(send_chunk);
    }
});

// Listen on all interfaces

// TODO HTTPS

if (!conf.interfaces || !conf.interfaces.http || !conf.interfaces.http.port) {
    console.error("Config file '" + process.argv[3] + "' is missing http port"
            + " for servers::" + process.argv[2]);
    process.exit(1);
}

server.listen(conf.interfaces.http.port);

