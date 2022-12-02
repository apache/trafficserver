TinyGo example for ATS Wasm Plugin

[Coraza Proxy WASM](https://github.com/corazawaf/coraza-proxy-wasm) is WAF wasm filter built on top of [Coraza](https://github.com/corazawaf/coraza) and implementing [proxy-wasm ABI](https://github.com/proxy-wasm/spec)

To retrieve `plugin.wasm` for the Coraza Proxy WASM
* `docker pull ghcr.io/corazawaf/coraza-proxy-wasm:main`
* `docker container create ghcr.io/corazawaf/coraza-proxy-wasm:main '/plugin.wasm'`
* `docker cp d7f055b746bb:/plugin.wasm /usr/local/var/wasm/plugin.wasm`

Copy the yaml in this directory to `/usr/local/var/wasm/` and activate the plugin in `plugin.config`
* `wasm.so /usr/local/var/wasm/plugin.yaml`

After restarting ATS, any request with `perl` as the user agent will retreive a status 403 response.
