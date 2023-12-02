# First steps

I thought about doing this in a Web Worker to provide isolation and a fresh realm, and then record the I/O to the worker to later replay to reconstruct a snapshot. However:

- WebWorkers are not very isolated. They have access to make network requests, for example. They're also born into a fresh environment that is polluted with existing globals, which presumably will be different between node and the browser (using a worker library in node.js). In short, it would be hard to make sure that it's deterministic.

- The worker's onmessage handler appears as a global. It makes it hard for the infrastructure to intercept it while also having the guest itself use a similar pattern for receiving messages. Unless I create nested threads.

- Worker threads are great and all, but they force the communication to be asynchronous, which is not necessarily what you want. I don't know exactly what I want, to be honest, but I know that asynchrony would prevent the host having a proxy object that represents an object in the guest. It changes the ergonomics of accessing it. Also I feel that it might be better if multi-threading and sandboxing were orthogonal -- you could run a sandbox in a thread if you want it asynchronous, or run a sandbox outside a thread if you want it synchronous, or run a thread without a sandbox if you want it asynchronous but not isolated, etc.

So the approach I'm going to try "quickly" is to see how hard it is to compile XS using emscripten. This should in principle give me a full JavaScript environment but I can snapshot it by recording the heap state of the XS engine and then restoring it later. This is what [@agoric/xsnap](https://www.npmjs.com/package/@agoric/xsnap) does.

## Why not use xsnap directly?

Because when I tried to install it, it wasn't friendly. It downloads a ton of dependencies - after installing in an empty folder, the folder has 253 packages. It has `yarn` as one of its build dependencies (some nested package.json `script` use yarn) but it doesn't install it. It seems like it's massive. The `npm install` takes forever. And then when I ran the simplest example, it didn't work. The package also has no link to where you can submit an issue. That was on mac. On Windows, the package doesn't even install.

If it just worked, I'd probably use it. But since it doesn't work, I'll also consider its other limitations:

- It doesn't support synchronous access to the guest.
- I'm pretty confident it isn't designed to run in a browser.

The mountain of dependencies is the main problem. I have no hope of using this library in a real project for work (which was my intention).

Oh the other issue is that it seems to actually download the moddable source code and presumably compile it.

I will note, however, that agoric seem to be using their own fork of moddable, at `https://github.com/agoric-labs/moddable.git`.

Also, xsnap seems to be bringing in this on as well: https://github.com/agoric-labs/xsnap-pub.git. This appears to be the native host.

Ok, they call it a "runtime". That makes sense.

The have an "additions" section in the readme that says the following are added "besides ECMAScript built-ins and intrinsics":

- `gc()`
- `print(x)`
- `harden(it)`
- `lockdown()`
- `currentMeterLimit()`
- `resetMeter(newMeterLimit, newMeterIndex)`
- `TextDecoder`
- `TextEncoder`
- `clearImmediate(id)`
- `clearInterval(id)`
- `clearTimeout(id)`
- `setImmediate(callback)`
- `setInterval(callback, delay)`
- `setTimeout(callback, delay)`
- `performance.now()`

I pretty much don't agree with adding any of these to my library. The guest shouldn't be able to reset its own meter. I don't want builtin timer capabilities and I'm not even sure how emscripten will compile timers.

It looks like Moddable's main branch already supports metering, so this is not a capability in agoric's fork (or maybe it was merged).

I don't care to introduce hardening or compartments.

From the readme, it looks to me like xsnap is intended to be used as a CLI. Indeed, it looks like the library is essentially spinning up a child process to run the CLI.

So probably the emscripten build is going to take a bit of work to run as a library instead of CLI.

The CLI is in `xsnap-worker.c`. It receives commands as single ascii characters on the stdio. Search for `switch(command)`. The command `'e'` means to execute a script, and is essentially what the the `worker.evaluate()` example in the xsnap readme translates to. It's implemented as a call to the global eval, within a try-catch. Search for `xsVar(1) = xsCall1(xsGlobal, xsID("eval"), xsVar(0))`.

I'm optimistic because this looks pretty easy to implement. I don't think I need to fork xsnap, because the pieces I want are actually very simple.

The return value of `eval` is not really used. Or it seems that if it returns an array buffer then that's given back to the host.

What I would like is that the return value of `eval` anchored to a GC handle and given a dynamic address, allowing me to send messages to it (call it). I think that's a better pattern than the global `onmessage` handler of a Web Worker. It allows individually-evaluated scripts to messaged separately.

Snapshotting seems to be conveyed by the `w` command which calls `fxWriteSnapshot`. Surprisingly, this seems to be a builtin part of Moddable's XS as well. That's surprising but it's actually a good thing for this POC because it means we don't need a special branch of moddable's engine.


## Emscripten

So, I've installed emscripten to try do some compiling. I'm first going to try compile a simple hello-world script before I actually try compile XS, because it's been many years since I used emscripten.

I'm working through this tutorial: https://emscripten.org/docs/getting_started/Tutorial.html

Ok, hello world works. But, it's routed the printf automatically to stdout, which I don't think I want. Or maybe I do. Let me ignore that.

It looks like the module and its bindings are added to `globalThis` by default. ChatGPT says I can compile with `-o your_module.mjs -s MODULARIZE=1 -s EXPORT_ES6=1` to produce a module factory instead, which can then be imported and run to instantiate the module.

`emcc test/hello_world.c -o hello_world.mjs -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORTED_FUNCTIONS='["_your_function"]'`

Hmm. In [this documentation](https://emscripten.org/docs/porting/guidelines/portability_guidelines.html) it says that `setjmp` and `longjmp` don't work properly in emscripten. I know that moddable uses this for exception handling, so I wonder if exceptions will work properly. Oh actually I misread. It says it does support "proper" setjmp/longjmp. So hopefully it will be fine.

It looks like I will need to compile with `-sDISABLE_EXCEPTION_CATCHING=0` if I want exceptions to work, but I'm not sure.

I see the example documentation also adds `-sEXPORTED_RUNTIME_METHODS=ccall,cwrap`

---

Ok, after some work, I've managed to get an example working where I have a `process_message` function that accepts a dynamically-sized buffer and returns another, with apparently the correct memory management to pass this across the boundary.

I'm using this with a dummy implementation that treats the buffer as an arbitrary ascii string, but the intention is to use this to pass JSON, ultimately. Although I may not implement that myself, but I'll see.