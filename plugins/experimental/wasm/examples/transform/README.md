CPP example for handling request/response transform for ATS Wasm Plugin

To compile `transform.wasm`
* Create a docker image following instructions here - https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/blob/master/README.md#docker
* In this directory run `docker run -v $PWD:/work -w /work  wasmsdk:v2 /build_wasm.sh`

Copy the yaml in this directory and the generated `transform.wasm` to `/usr/local/var/wasm/` and activate the plugin in `plugin.config`
* `wasm.so /usr/local/var/wasm/transform.yaml`

