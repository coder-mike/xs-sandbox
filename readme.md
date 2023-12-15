# XS Sandbox

*Note: this library has not been published to npm yet.*

Secure and easy JavaScript sandbox with snapshotting support, with no dependencies or native modules. Compatible with Node.js or browser environment.

Internally uses a WASM build of the XS JavaScript engine (guest scripts are not running in the same engine as the host and so are completely isolated). It's reasonably lightweight -- each instance is a few MB.


## Usage: Evaluating Scripts

```js
import Sandbox from 'xs-sandbox';

const sandbox = await Sandbox.create();
const result = sandbox.evaluate('1 + 1');
console.log(result); // 2
```

## Usage: Snapshotting

```js
import Sandbox from 'xs-sandbox';

const s1 = await Sandbox.create();
s1.evaluate('var x = 1');
console.log(s1.evaluate('++x')); // 2
console.log(s1.evaluate('++x')); // 3

const snapshot = s1.snapshot(); // snapshot as Uint8Array

// Later, or on another machine
const s2 = await Sandbox.restore(snapshot);
console.log(s2.evaluate('++x')); // 4
```

An empty snapshot is about 85kB.

## Usage: Message passing

```js
import Sandbox from 'xs-sandbox';

const sandbox = await Sandbox.create();

// Receive messages from the sandbox like this:
sandbox.receiveMessage = function (message) {
  console.log(`Received message from sandbox: ${message}`)
}

sandbox.evaluate(`
  // Receive messages from the host like this:
  globalThis.receiveMessage = function (message) {
    // Send messages to the host like this:
    sendMessage('Received message from host: ' + message);
  }
`);

// Send a message to the guest like this:
sandbox.sendMessage('Message from host');
```

Messages are encoded as JSON to be sent to/from the sandbox so only plain data types are supported.

Messages are passed *synchronously*. If you want asynchronous behavior you can wrap the sandbox in a `Worker` thread.

## Usage: Metering

Metering allows you to set a limit on the amount of processing that the guest can do, which can be useful to catch infinite loops, especially if you don't trust the guest code. The `sandbox.meter` counter counts up as the guest executes instructions. If the limit is reached, the guest will halt and the sandbox will throw an exception.

```js
import Sandbox from 'xs-sandbox';

const sandbox = await Sandbox.create({
  meteringLimit: 5000,
  meteringInterval: 1000,
});

try {
  sandbox.evaluate('while (true) {}') // Infinite loop
} catch (e) {
  console.log(e); // "metering limit reached"
  console.log(sandbox.meter);
}
```

Be careful with meter limits because the limit can be hit at any time and it halts the machine without processing any catch blocks in the guest code, which may leave the guest in an inconsistent state. It is strongly recommended not to use the sandbox again after it has hit a metering limit.



## Promises and the event loop

The guest event loop is processed synchronously by `evaluate` and `sendMessage`, so there will never be pending jobs when `sendMessage` returns. Example:

```js
import Sandbox from 'xs-sandbox';

const sandbox = await Sandbox.create();
sandbox.receiveMessage = console.log;

sandbox.evaluate(`
  async function myAsyncFunc() {
    sendMessage('before await');
    await Promise.resolve();
    sendMessage('after await');
  }

  sendMessage('before calling myAsyncFunc');
  myAsyncFunc();
  sendMessage('after calling myAsyncFunc');
`);
console.log('after evaluate');
```

This prints:

1. before calling myAsyncFunc
2. before await
3. after calling myAsyncFunc
4. after await
5. after evaluate

Points 4 and 5 show that the promise job queue is processed before `evaluate` returns.


## Guest Environment and Globals

The environment in which the guest script runs is a vanilla ECMAScript environment with no I/O APIs except `sendMessage` and `receiveMessage`. You can define your own APIs for the guest by first evaluating your own setup script which implements APIs in terms of `sendMessage` and `receiveMessage`.

**None of the host environments globals are available to the guest script!**


## Known issues

- I've had issues under node.js 18 with the WASM crashing, but it seems to be working ok in the browser and in node.js 20 (in Windows).
- When building without optimization, there's a weird behavior where the `fxRunID` C function (compiled to WASM) seems to cause a spike in memory usage by multiple GB for a few seconds, and if running in Node then the node process takes some time to exit at the end (tens of seconds). The released version on npm is built with optimization and so doesn't have this problem.


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

To build the project:

```sh
npm install
npm run build
```


## License

The source code in this project is MIT licensed. The project incorporates [a fork](https://github.com/coder-mike/moddable) of [Moddable's runtime engine](https://github.com/Moddable-OpenSource/moddable) as a git submodule, which is under the GNU Lesser General Public License v3 and Apache License Version 2.0. The fork only contains 4 line changes to get it to compile and disable stack checking which seems to be incompatible with the WASM runtime. Please refer to the Moddable SDK license information.

The distributed package on NPM incorporates all 3 licenses.

A copy of the licenses from Moddable SDK is distributed under [./moddable-sdk-licenses](./moddable-sdk-licenses) for reference. The MIT license is available at [./MIT-LICENSE](./MIT-LICENSE).