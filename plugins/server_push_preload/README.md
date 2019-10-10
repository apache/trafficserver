Parse origin response Link headers and use H2 Server Push to initiate push requests of assets that have the preload keyword.

https://www.w3.org/TR/preload/

This plugin can be used as a global plugin or a remap plugin.

To use it as a global plugin for all your remaps, add this line to your `plugins.config` file:

```
server_push_preload.so
```

To use it as a remap plugin add it to one of your remaps in the `remap.config` file:

```
map https://foo.cow.com/ https://bar.cow.com @plugin=server_push_preload.so
```