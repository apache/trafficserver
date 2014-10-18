var log = function(thresh, data) {
    // print to stderr to keep distinct from generate test report
    console.error(thresh + ': ' + data);
};

var repo_root = '../..';
var tf = require(repo_root + '/tests/framework/atstf.js').atstf_new({
    ats_path: repo_root + '/proxy/traffic_server',
    origin_path: repo_root + '/tests/framework/origin.js',
    config_path: './config.json',
    log_cb: log
});
var assert = require("assert");
var client = require('http').request;

// See http://visionmedia.github.io/mocha for details on this test framework.

describe('Example', function() {
    before(function(done) {
        tf.start(done);
    });

    after(function(done) {
        tf.stop(done);
    });

    it('ats1', function(done) {
        // Where is ats1?
        var host = tf.config.servers.ats1.interfaces.http.hostname;
        var port = tf.config.servers.ats1.interfaces.http.port;
        
        // Path to origin1
        var path = '/foo/bar';
        
        // What is origin1 configured to send?
        var action = tf.config.servers.origin1.actions.GET[path];
        var status_code = action.status_code;
        var bytes_to_receive = action.chunk_size_bytes * action.num_chunks;
        var chunk_byte_value = action.chunk_byte_value;

        var req = client(
                {
                    hostname: host,
                    port: port,
                    path: path,
                    method: 'GET'
                },
        function(res) {
            log('DEBUG', 'STATUS: ' + res.statusCode);
            log('DEBUG', 'HEADERS: ' + JSON.stringify(res.headers));
            
            var bytes_received = 0;
            
            assert.equal(status_code, res.statusCode);

            res.on('data', function(chunk) {                    
                for (var i = 0; i < chunk.length; ++i) {
                    assert.equal(chunk_byte_value, chunk[i]);
                }
                
                bytes_received += chunk.length;
                
                if (bytes_received >= bytes_to_receive) {
                    // End test (or timeout and fail)
                    done();
                }
            });
        });

        req.on('error', function(e) {
            assert.fail(e, null, 'socket error');
        });

        req.end();
    });

    it('ats2', function(done) {
        var host = tf.config.servers.ats2.interfaces.http.hostname;
        var port = tf.config.servers.ats2.interfaces.http.port;
        var path = '/foo/bar';

        var req = client(
                {
                    hostname: host,
                    port: port,
                    path: path,
                    method: 'GET'
                },
        function(res) {
            log('DEBUG', 'STATUS: ' + res.statusCode);
            log('DEBUG', 'HEADERS: ' + JSON.stringify(res.headers));

            assert.equal(404, res.statusCode);

            done();
        });

        req.on('error', function(e) {
            assert.fail(e, null, 'socket error');
        });

        req.end();
    });
});
