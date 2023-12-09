# JS Snapshotting

This is an experiment to create a JavaScript sandbox (in a JavaScript host) that can be snapshotted.

This is under development. Have a look at [.dev-journal/](./.dev-journal/2023-12-02%20first%20steps.md) for my notes as I develope this.

Note: I've had issues under node.js 18 with the WASM crashing, but it seems to be working ok in the browser and in node.js 20 (in Windows).

Example:

```js
import Sandbox from 'xs-sandbox';

const sandbox = new Sandbox();

sandbox.receiveMessage = function (message) {
  console.log(`Received message from sandbox: ${message}`)
}

sandbox.evaluate(`
  globalThis.receiveMessage = function (message) {
    sendMessage('Received message from host: ' + message);
  }
`);

sandbox.sendMessage('message from host');

const snapshot = sandbox.snapshot();

// Later, or on another machine
const anotherSandbox = Sandbox.restore(snapshot);
anotherSandbox.receiveMessage = function (message) {
  console.log(`Received message from sandbox: ${message}`)
}
```

Within the sandbox, there are two special globals:

1. `receiveMessage` may be defined by the guest in order to receive messages sent by the host.
2. `sendMessage` may be called by a guest to send messages to the host.

Messages are sent between the host and the guest as JSON, so they cannot contain references to functions, etc.


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

## License

The source code in this project is MIT licensed. The project incorporates [a fork](https://github.com/coder-mike/moddable) of [Moddable's runtime engine](https://github.com/Moddable-OpenSource/moddable) as a git submodule, which is under the GNU Lesser General Public License v3 and Apache License Version 2.0. The fork only contains 4 line changes to get it to compile and disable stack checking which seems to be incompatible with the WASM runtime. Please refer to the Moddable SDK license information.

The distributed package on NPM incorporates all 3 licenses.

A copy of the licenses from Moddable SDK is distributed under [./moddable-sdk-licenses](./moddable-sdk-licenses) for reference. The MIT license is available at [./MIT-LICENSE](./MIT-LICENSE).