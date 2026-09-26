# Examples

The same small app three times — a header bar and a row of buttons floating on
glass over colourful tiles that scroll under them, one capsule tuned with a
parameter of its own — in each language glass-lib supports.

| File | Run from the repository, after `meson compile -C build` |
|---|---|
| [`hello-glass.c`](hello-glass.c) | `build/examples/hello-glass` |
| [`hello-glass.py`](hello-glass.py) | `meson devenv -C build -w . python3 examples/hello-glass.py` |
| [`hello-glass.js`](hello-glass.js) | `meson devenv -C build -w . gjs -m examples/hello-glass.js` |

`meson devenv` puts the build's library and `Glass-1.typelib` on the paths; with
glass-lib installed, run them directly. The C one also builds on its own:

```sh
cc hello-glass.c -o hello-glass $(pkg-config --cflags --libs glass-lib-1)
```

For more — every part, the Lab with all the parameters, a weather app — see the
demos in [`demo/`](../demo).
