CPP example for making asynchronous requests for ATS Wasm Plugin

To compile `async.wasm`
* Create a docker image following instructions here - https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/blob/master/README.md#docker
* In this directory run `docker run -v $PWD:/work -w /work  wasmsdk:v2 /build_wasm.sh`

Copy the yaml in this directory and the generated `async.wasm` to `/usr/local/var/wasm/` and activate the plugin in `plugin.config`
* `wasm.so /usr/local/var/wasm/myproject.yaml`

Make sure you have mapping rules in remap.config like below
* `map https://www.google.com/ https://www.google.com/

You can trigger the asynchronous requests with a request with User-Agent set to `test`
