# JS Snapshotting

This is an experiment to create a JavaScript sandbox (in a JavaScript host) that can be snapshotted.

This is under development. Have a look at [.dev-journal/](./.dev-journal/2023-12-02%20first%20steps.md) for my notes as I develope this.

Note: I've had issues under node.js 18 but it seems to be working ok in the browser and in node.js 20 (in Windows).

## Maintainer notes

This depends on `moddable` as a git submodule. It uses a fork of the original moddable repo because there were a few changes I needed to make to get it to work. The biggest change is that I had to disable stack checks in various places because the stack depth calc didn't seem to work in WASM.

To clone this project, clone it with submodules:

```sh
git clone --recurse-submodules ...
```

Or if you've already cloned without submodules, run:

```sh
git submodule update --init --recursive
```

I'm on using Windows, using mingw make. To build the project:

```sh
mingw32-make clean
mingw32-make
```