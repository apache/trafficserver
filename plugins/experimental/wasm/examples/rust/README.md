RUST example for ATS Wasm Plugin 

To set up a rust environment
* `curl https://sh.rustup.rs -sSf | sh`
* `rustup toolchain install nightly`
* `rustup target add wasm32-unknown-unknown`

To compile `myfilter.wasm`
* Under this directory run `cargo build --target=wasm32-unknown-unknown --release`

Copy the yaml in this directory and the generated `myfilter.wasm` to `/usr/local/var/wasm/` and activate the plugin in `plugin.config`
* `wasm.so /usr/local/var/wasm/myfilter.yaml`

