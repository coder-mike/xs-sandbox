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

The `issueCommand` is conveyed by command code `?`. In the runtime this is implemented by calling the global `handleCommand` function.


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

---------

Ok, I think next I want to just think briefly about how I intend this to actually work.

I want something similar to what xsnap presents but synchronous. Basically:

```js
import JSSnap from 'js-snap';

const worker = await JSSnap.create();

const adder = worker.evaluate(`(function adder(a, b) { return a + b })`);
const c = adder(a, b);

const snapshot = worker.snapshot();
```

If there are any jobs in the job queue, I think these should be processed synchronously similarly to Microvium. But let me get the basics working before I figure that out.

In this example, `adder` will somehow hold a reference to the evaluated value inside the container. ChatGPT says you can do this with `xsRemember` and `xsForget`.

Maybe rather than arbitrary functions, we should say that the function is specifically a message receiver. So it will only have one parameter. The only reason is to make clear that we're not just looking at a normal function call. But maybe that doesn't matter. It's pretty versatile.

Oh there's a problem with this idea which is that the `adder` reference will need to be reconstructed after a snapshot. That's not impossible but it would be simpler for the library to not have dynamic references at all and leave that to the user layer.

So an alternative is to have a global `onmessage` variable that we post to. Or xsnap uses the global name `handleCommand` it seems.

```js
import JSSnap from 'js-snap';

const worker = await JSSnap.create();

worker.evaluate(`globalThis.handleCommand = function (command) { }`);
worker.postCommand('...');

const snapshot = worker.snapshot();
```

It's at this point that I realize that I need TextEncoder and TextDecoder if I want to send binary and interpret it as JSON. Presumably I will either be transcoding in C or just sending the strings directly. Probably the latter, but I'll see.

So to recap, I'll have these 3 or C functions exported using emscripten. `evaluate` will take a string and return nothing. `postCommand` will either take and return strings or take and return POD objects. `snapshot` will return a buffer-like type like we already have working.

Ok, so with that out of the way, let's get to actually compiling moddable.

I think that like what xsnap did, I'm going to clone moddable in a separate directory.

`git clone https://github.com/Moddable-OpenSource/moddable.git`

The xsnap build script appears to do this:

```js
  const make = makeCLI(platform.make || 'make', { spawn });
  for (const goal of ModdableSDK.buildGoals) {
    // eslint-disable-next-line no-await-in-loop
    await make.run(
      [
        `MODDABLE=${ModdableSDK.MODDABLE}`,
        `GOAL=${goal}`,
        `XSNAP_VERSION=${pkg.version}`,
        `CC=cc "-D__has_builtin(x)=1"`,
        '-f',
        'xsnap-worker.mk',
      ],
      {
        cwd: `xsnap-native/xsnap/makefiles/${platform.path}`,
      },
    );
  }
```

Here `buildGoals` are `['release', 'debug']`, so maybe this is making both the build and release versions.

On Windows, `make` should be a CLI that points to `nmake`.

The path `xsnap-native/xsnap/makefiles/${platform.path}` should actually point to a windows makefile path, which doesn't appear to exist.

The xsnap worker makefile (for linux) is quite long.

I can see from the makefile that the text encoder and decoder are just builtin.

---

After some work, I have it building. But it gives me this warning:

```
wasm-ld: warning: function signature mismatch: fxNewHostInstance
>>> defined as (i32) -> void in C:\Users\elecm\AppData\Local\Temp\emscripten_temp_fqn30642\textdecoder_48.o
>>> defined as (i32) -> i32 in C:\Users\elecm\AppData\Local\Temp\emscripten_temp_fqn30642\xsAPI_3.o
```

I can see that the function indeed has 2 different signatures in different places in Moddable's code:

```
mxImport void fxNewHostInstance(xsMachine*);
mxExport txSlot* fxNewHostInstance(txMachine* the);
```

I'm going to modify the void-returning one. The actual implementation looks like it's returning a slot.

It's not a big enough change to warrant making a fork or a PR.

By the way, in Moddable I'm on commit `e95cde090ea079e79779246eea8611b0f8628cd4` from `Thu Nov 23 18:12:05 2023 +0900`

    On branch public
    Your branch is up to date with 'origin/public'.

    Changes not staged for commit:
      (use "git add <file>..." to update what will be committed)
      (use "git restore <file>..." to discard changes in working directory)
            modified:   xs/includes/xs.h

    no changes added to commit (use "git add" and/or "git commit -a")

The WASM file is 20kb 😂. Of course that means it's optimized away all Moddable's code.

----

Ok, so the next step is actually to invoke XS.

----

I don't know why, but it seems XS has a *ton* of signature conflicts between the signatures in `xs.h` and in `xsAll.h` (and its imports such as `xsCommon.h`) such as the following:

```
moddable/xs/sources\xsCommon.h:471:19: error: conflicting types for 'fxIntegerToString'
  471 | mxExport txString fxIntegerToString(void* dtoa, txInteger theValue, txString theBuffer, txSize theSize);
      |                   ^
moddable/xs/includes\xs.h:1513:24: note: previous declaration is here
 1513 | mxImport xsStringValue fxIntegerToString(xsMachine*, xsIntegerValue, xsStringValue, xsIntegerValue);
```

It's not clear at all how `dtoa` is meant to be related to `xsMachine`.

There's too many of these for me to go through them. I mean, I could, but it would take a while and it's throw-away changes because if I pull the source again at a later stage then I'll lose my changes. And I have no confidence in how these are meant to be resolved.

I'm going to just resolve this by bringing in the few declarations I need from xsAll.

----

Urgh. This strategy isn't working. There are *SO* many things that need declarations from xsAll. For example, `mxPushUndefined` is in `xsAll.h`. When I copy it across, it depends on access to the stack which is also not available through `xs.h`.

By the way, it seems to me that `xs.h` is the public interface and `xsAll.h` is the private interface.

Ok, so I'm going to try something a bit different. Clearly XS itself tends not to import `xs.h` otherwise the inconsistencies would show up everywhere. So I think that units in the program can basically only import one or the other but not both. I will commit my work-in-progress on this failed port and then try something like this:

- The `wedge.c` unit will be entirely an "internal" unit, importing `xsAll.h` internally so it has all of those mechanics.
- The `wedge.h` header be agnostic of `xs.h`/`xsAll.h` (it won't import either)

------

Ok, finally, it's building and able to call `initMachine` and do the run loop. The WASM file is 3MB. Holy shit that's big. I'm curious how big the C build would be.

To get it working, I needed to copy in a lot of the `xsnapPlatform.c`. It took many hours in total to get it working, even though I didn't need to write the platform code myself.

Having done it, I'm almost surprised that it worked.

Anyway. The next step is to run it.

---

Ok, I get an OOM. Looks like I can add `-sALLOW_MEMORY_GROWTH`.

It was asking for 1,074,167,808 bytes. That's probably a bit much. The initial memory is currently `16,777,216`, which is much more reasonable.

The other thing is that the chunk allocator I copied across was doing some complicated `mmap` thing, and I've changed it to just use malloc.

---

Wow! `Hello, World` is working again. It's not using the machine, but it's calling the machine initialization.

It's very slow. I don't know what it's doing.

It seems like the majority of the time is spent initializing the wasm module. That's a little concerning.

Ah it's ok, it initializes much faster a second time. So there must be some once-off cost in loading and compiling the WASM.

---

Side note: I discovered the `SINGLE_FILE` emscripten option which builds the WASM into the library JS file. It doesn't feel much slower. The library is a bit bigger now, at `4,164kB`.

Oh but I don't have optimization enabled. With `-Os` optimization, it's `1,423kB`, which is quite a lot better. Also it loads and runs significantly faster now.

-----

I think the next step is to actually run the machine.

-----

It doesn't work. It says it's throwing a JavaScript exception but the exception is undefined.

---

I've added various logging but unable to really dig into it. I can't see a way around this other than to create the project in VC++ and actually step through it in a debugger.

----

*sigh*. the project uses `sys/time.h` and `arpa/inet.h` and other posix libraries, which VS doesn't have. So just to debug this I'm going to need to make the platform file portable. Hopefully that's not too difficult. That's really taken the wind out of my sails.

----

2023-12-03 16:39 That took a long time to work through all the errors. I'm now finally launching the debugger.

Ok, for whatever reason, in the case of a type error, if you don't enable debugging then instead of throwing an exception it will just jump to the catch handler.

When I enable debug, I get an error on this line `txSlot* scope = scope;`. I'm changing it to `= 0`.

Ok, I think I misunderstood the code. It will jump anyway, because of course it does. The exception is still "undefined".

Ok, I think maybe the issue is my variable name. The macros seem to assume that the machine is accessible with the name "the".

Oh actually it looks like `xsBeginHost` sets up the `the` variable.

When I call `fxToString(xsException)`, it thinks that the slot kind is zero which is undefined. But I clearly see that earlier it's setting it to 0xa which is a reference.

I'm going to try look at the addresses.

Ok, the apparent size of a slot changes. Yes, that will cause a problem.

The public interface is using this definition:

```c
struct xsSlotRecord {
	void* data[4];
};
```

Yikes, that's a weird definition. 4 pointers. Anyway, that's 16 bytes.

The internal structure is this:

```c
struct sxSlot {
	txSlot* next;
#if mx32bitID
	#if mxBigEndian
		union {
			struct {
				txID ID;
				txS2 dummy;
				txFlag flag;
				txKind kind;
			};
			txS8 ID_FLAG_KIND;
		};
	#else
		union {
			struct {
				txKind kind;
				txFlag flag;
				txS2 dummy;
				txID ID;
			};
			txS8 KIND_FLAG_ID;
		};
	#endif
#else
	#if mxBigEndian
		union {
			struct {
				txID ID;
				txFlag flag;
				txKind kind;
			};
			txS4 ID_FLAG_KIND;
		};
	#else
		union {
			struct {
				txKind kind;
				txFlag flag;
				txID ID;
			};
			txS4 KIND_FLAG_ID;
		};
	#endif
	#if INTPTR_MAX == INT64_MAX
		txS4 dummy;
	#endif
#endif
	txValue value;
};
```

The size of this is apparently 24 bytes. I'm compiling for 32-bit platform by the way.

Ok, I think it's the value `txS8 KIND_FLAG_ID;` which is an 8-byte value that follows a 32-bit value, but there's nothing to mark the struct as packed.

---

2023-12-04 07:27 I added the compiler settings in VS to automatically pack structures to 4-byte alignment. But then I get errors in winnt.h because it asserts "Windows headers require the default packing option. Changing this can lead to memory corruption. This diagnostic can be disabled by building with WINDOWS_IGNORE_PACKING_MISMATCH defined."

I don't know if I'm really going to encounter corruption issues. I'm going to give the flag "WINDOWS_IGNORE_PACKING_MISMATCH".

Oh actually with 4-byte packing, the size is 20 bytes instead of 24, but still not 16. Oh actually even with 1 byte packing.

Fuck I don't understand this code. I recall from memory that the xs slot size is 16 bytes on a 32-bit machine, and I can see the definition of `xsSlotRecord` confirms that, with the size being 4 machine words which is 16 bytes. But then their convoluted `sxSlot` type essentially boils down to this:

```c
struct sxSlot {
	txSlot* next;
	union {
		struct {
			txKind kind;
			txFlag flag;
			txS2 dummy;
			txID ID;
		};
		txS8 KIND_FLAG_ID;
	};
	txValue value;
};
```

The middle union clearly takes at least 8 bytes, because of the `txS8`. The `next` must be 32 bits. But then `value` here takes up 8 bytes!

Ah, I can see the issue. The `KIND_FLAG_ID` field either takes 4 or 8 bytes. It takes 8 bytes if `mx32bitID` is true and 4 bytes otherwise. That seems bloody backwards to me.

Anyway, I inherited `mx32bitID` from the xsnap platform code. I'm removing it now.

Fuck what a long road, but now the sizes match and I can see `Result of eval: Hello, from XS!` so the script is now evaluating, at least in VS. Let me try in emscripten as well.

2023-12-04 07:54 Ok, the WASM run isn't working. It's a different failure condition to last time. It's now repeatedly saying `adjustSpaceMeter: 1024` and eventually failing with `RuntimeError: table index is out of bounds`.

Given that I had it working in VS, I have some confidence that my usage code is ok and it's the platform setup that's wrong. I'll start by checking the type sizes.

2023-12-04 08:05 The slot sizes match and are both 16 bits. I guess I don't have much choice but to add logging to see where it fails.

2023-12-04 08:50 The hardest part about this is that it's non-deterministic. It's like there's corruption but it's random. Incredibly hard to pin down. The WASM itself should be deterministic, so I assume the issue is that the inserted print statements are breaking things.

I'm going to try try disable optimization and see what happens.

2023-12-04 08:56 When I disable optimization, it gets a lot further and then aborts for some reason. I'm going to remove all my old debug prints since they doing help at all with the new abort position.

2023-12-04 09:02 Ok, it seems to be aborting due to an apparent stack overflow. I kinda doubt that it's really stack-overflowing.

It's in `fxCheckCStack` that it's detecting the overflow. That function is called from a lot of places so it's not immediately obvious where I should look next.

The overflow is happing during the `xsVar(1) = xsCall1(xsGlobal, xsID("eval"), xsVar(0))` call.

Just to confirm, I bumped up the stack count by 8x and it still overflows. But I know in VS it doesn't overflow even at the current count, so there is something else wrong.

What kind of problem would cause the symptoms we're seeing?

- There is an inherent flaw in the engine, which is only uncovered because the WASM target is sufficiently unusual.
- There's something wrong with my port configuration that's causing something to be misaligned or something to that effect. Or mismatched endianess.

I can't see anything obvious. The slot size appears correct. The endianness is marked as little endian, the same way in Visual Studio and Emscripten (through the `#define`).

Maybe the next step is to get debugging working in Emscripten so I can step through it and see where it fails. Maybe it will become obvious. The documentation does say that emscripten can produce debug symbols. https://emscripten.org/docs/porting/Debugging.html

Also there are a bunch of extra checks I can enable in emscripten. I'm going to enable those and see if it changes anything. (it didnt)

--------

2023-12-05 07:27 I've compiled with debug symbols and stepped in the browser, but it's not working. I seem to recall issues with debug symbols when I was doing the Microvium WASM library.

I'm trying various combinations of compiler flags and directory changes.

Ok, it's working.

I'm not 100% sure what fixed it:

- I think that at some point I switched from naming the output directory `out` to `dist` but then got confused because there were some old files sticking around (the local web server had a lock on them) and I may have been looking at the wrong ones.
- I've added flags `-g3` and `-fdebug-compilation-dir=.` to both the compiler and linker. Not sure if one or both uses either of these, but just added on both to be sure.
- I compiled the output to just to into the project root `./` to avoid some kinds of directory issues.

Now I'm going to try output into the `dist` folder and see if it still debugs.

It doesn't seem to. I'm going to keep it in the dist folder and change the arg to `-fdebug-compilation-dir=..`.

Ok, yes, I can see that now it's loading symbols. In the console in the browser, it says:

```
Initializing wasm module...
[C/C++ DevTools Support (DWARF)] Loading debug symbols for http://localhost:3000/dist/mylib.wasm...
[C/C++ DevTools Support (DWARF)] Loaded debug symbols for http://localhost:3000/dist/mylib.wasm, found 261 source file(s)
```

2023-12-05 07:55 The safe-heap stuff is getting in the way, so I'm going to disable that for the moment.

`fxRunID` is very hard to debug. It takes about 30 seconds to step. Maybe because it's such a big function.

Anyway, it seems to be crashing in `mxCheckCStack` right at the beginning of `fxRunID`. That's a pretty good place to be crashing.

Debugging is not fantastic. When I add a Watch for `the->stackLimit`, it tells me `the->stackLimit: "~"`.

Anyway, so the failing line so far is `(stack <= the->stackLimit)` -- it should be returning false but is returning true.

The `stackLimit` is set in `xsMemory.c` in `fxAllocate`.

Ah, interesting. It wasn't lying when it says this is the **C stack**. The implementation of `fCStackLimit` is:

```c
    char* result = C_NULL;
		pthread_attr_t attrs;
		pthread_attr_init(&attrs);
		if (pthread_getattr_np(pthread_self(), &attrs) == 0) {
    		void* stackAddr;
   			size_t stackSize;
			if (pthread_attr_getstack(&attrs, &stackAddr, &stackSize) == 0) {
				result = (char*)stackAddr + (128 * 1024) + mxASANStackMargin;
			}
		}
		pthread_attr_destroy(&attrs);
		return result;
```

Ok, I should have read that more clearly. Of course I don't expect the C stack to behave the same way in WASM.

It's a little bit annoying because the behavior is controlled by `mxBoundsCheck` which also checks for overflows using `fxOverflow`. But anyway, I'll disable this.

2023-12-05 08:27 Ok, I guess it's progress, but I'm not sure. I have the following printout:

```
checking stack in fxRunID 0
checked stack  in fxRunID 0
Metering callback
Metering callback
# exception: throw!
Caught an exception
checking stack in fxRunID 1
checked stack  in fxRunID 1
checking stack in fxRunID 0
checked stack  in fxRunID 0
Exception: RangeError: stack overflow
```

And then later WASM itself crashes:

```
#
# Fatal error in , line 0
# Check failed: IdField::is_valid(id).
#
#
#
#FailureMessage Object: 000000A9BF3FE640
 1: 00007FF745DBAEFF node_api_throw_syntax_error+206127
 2: 00007FF745CD15DF v8::CTypeInfoBuilder<void>::Build+11951
 3: 00007FF746B39492 V8_Fatal+162
 4: 00007FF746B71F4A v8::internal::compiler::HeapConstantType::Value+666
 5: 00007FF746C257DA v8::internal::compiler::Graph::NewNodeUnchecked+58
 6: 00007FF746BCDDE2 v8::internal::compiler::MachineOperatorReducer::Int32Constant+6146
 7: 00007FF7462001DC v8::internal::wasm::BuildTFGraph+110092
 8: 00007FF7461E4D28 v8::internal::wasm::JumpTableAssembler::NopBytes+20904
 9: 00007FF7461E47CD v8::internal::wasm::JumpTableAssembler::NopBytes+19533
10: 00007FF7461E4B98 v8::internal::wasm::JumpTableAssembler::NopBytes+20504
11: 00007FF7461E7EF4 v8::internal::wasm::BuildTFGraph+11044
12: 00007FF7461EB0BF v8::internal::wasm::BuildTFGraph+23791
13: 00007FF7461E62CD v8::internal::wasm::BuildTFGraph+3837
14: 00007FF7461E5589 v8::internal::wasm::BuildTFGraph+441
15: 00007FF746BCA027 v8::internal::compiler::CompileWasmImportCallWrapper+10615
16: 00007FF746201D1C v8::internal::wasm::WasmCompilationUnit::ExecuteFunctionCompilation+1324
17: 00007FF746201725 v8::internal::wasm::WasmCompilationUnit::ExecuteCompilation+229
18: 00007FF7461D5A8D v8::internal::wasm::CompileToNativeModule+4413
19: 00007FF7461DBA5E v8::internal::wasm::CompilationState::New+7662
20: 00007FF74694F260 v8::internal::SetupIsolateDelegate::SetupHeap+1437648
21: 00007FF745CD3FAD X509_STORE_CTX_get_lookup_certs+813
22: 00007FF745E0824D uv_poll_stop+557
23: 00007FF746DC6880 v8::internal::compiler::ToString+146448
24: 00007FFED0C87344 BaseThreadInitThunk+20
25: 00007FFED0DC26B1 RtlUserThreadStart+33
```

Yikes.

The `# exception: throw!` line may be generated by `fxReportException`. Yes it is.

The call stack in the debugger shows that this is a nested call to fsRunID. The first fsRunID is calling `fs_eval`. That sounds correct. Then that's calling `fxRunScript`. I can see it does the parsing and running. That `fxRunScript` is then calling `fxRunID` again. And then that's calling the throw opcode `XS_CODE_THROW`.

I'm not sure why the script would compile to have a throw instruction. Here are some theories:

1. The script compiler adds some wrapper code that includes a default throw.
2. There is no throw, and we're just witnessing the program executing corrupt bytecodes which happen to look like a throw in this case.
3. The compiler has had some other error that causes it to emit a throw instead of the actual bytecode (e.g. throwing a syntax error).

I'm going to breakpoint the places in the compiler where it outputs throw codes.

Ok, I think it's this:

```
	if (parser->errorCount) {
		if (parser->console) {
			coder.firstCode = NULL;
			coder.lastCode = NULL;
			fxCoderAddByte(&coder, 1, XS_CODE_GLOBAL);
			fxCoderAddSymbol(&coder, 0, XS_CODE_GET_VARIABLE, parser->errorSymbol);
			fxCoderAddByte(&coder, 2, XS_CODE_NEW);
			fxCoderAddString(&coder, 1, XS_CODE_STRING_1, mxStringLength(parser->errorMessage), parser->errorMessage);
			fxCoderAddInteger(&coder, -3, XS_CODE_RUN_1, 1);
			fxCoderAddByte(&coder, -1, XS_CODE_THROW);
		}
		else
			return C_NULL;
	}
```

I've added a printout `printf("Parser error: %s\n", parser->errorMessage);`. It prints this:

```
Parser error: stack overflow
```

Ok, I think it's generated in `fxCheckParserStack` which here is not optional and does the same pointer checking as before.

I'm going to do a code edit here, adding a `return` statement to the beginning of `fxCheckParserStack.`

I guess this is progress. I see `Result of eval: Hello, from XS!`. But now I also get:


```
#
# Fatal error in , line 0
# Check failed: IdField::is_valid(id).
#
#
#
#FailureMessage Object: 000000700EBFE610
 1: 00007FF745DBAEFF node_api_throw_syntax_error+206127
 2: 00007FF745CD15DF v8::CTypeInfoBuilder<void>::Build+11951
 3: 00007FF746B39492 V8_Fatal+162
 4: 00007FF746B71F4A v8::internal::compiler::HeapConstantType::Value+666
 5: 00007FF746C257DA v8::internal::compiler::Graph::NewNodeUnchecked+58
 6: 00007FF746BCDDE2 v8::internal::compiler::MachineOperatorReducer::Int32Constant+6146
 7: 00007FF7462001DC v8::internal::wasm::BuildTFGraph+110092
 8: 00007FF7461E4D28 v8::internal::wasm::JumpTableAssembler::NopBytes+20904
 9: 00007FF7461E47CD v8::internal::wasm::JumpTableAssembler::NopBytes+19533
10: 00007FF7461E4B98 v8::internal::wasm::JumpTableAssembler::NopBytes+20504
11: 00007FF7461E7EF4 v8::internal::wasm::BuildTFGraph+11044
12: 00007FF7461EB0BF v8::internal::wasm::BuildTFGraph+23791
13: 00007FF7461E62CD v8::internal::wasm::BuildTFGraph+3837
14: 00007FF7461E5589 v8::internal::wasm::BuildTFGraph+441
15: 00007FF746BCA027 v8::internal::compiler::CompileWasmImportCallWrapper+10615
16: 00007FF746201D1C v8::internal::wasm::WasmCompilationUnit::ExecuteFunctionCompilation+1324
17: 00007FF746201725 v8::internal::wasm::WasmCompilationUnit::ExecuteCompilation+229
18: 00007FF7461D5A8D v8::internal::wasm::CompileToNativeModule+4413
19: 00007FF7461DBA5E v8::internal::wasm::CompilationState::New+7662
20: 00007FF74694F260 v8::internal::SetupIsolateDelegate::SetupHeap+1437648
21: 00007FF745CD3FAD X509_STORE_CTX_get_lookup_certs+813
22: 00007FF745E0824D uv_poll_stop+557
23: 00007FF746DC6880 v8::internal::compiler::ToString+146448
24: 00007FFED0C87344 BaseThreadInitThunk+20
25: 00007FFED0DC26B1 RtlUserThreadStart+33
```

I tried compiling with 64x the stack size to confirm that it's not overflowing the stack here, but it didn't help.

The error here says "WasmCompilationUnit::ExecuteFunctionCompilation", which strongly implies that this is not an issue execution but with compilation.

Ok, actually it's getting past the `process_message` call.

Oh, it works fine in the browser.

I think one problem is that I'm using `subarray` to get the result when I should be using slice.

That didn't make a difference.

Ok, so I have a "done" at the end of `run.mjs` and I can see that it's called before everything crashes. Quite a while before.

Ok, interesting. When I enable all the emscripten checks, I get this `alignment fault`:

```
begin metering...
begin host...
host vars...
begin try...
Script prepared for eval
fxRunID 1
Heap resize call from 1310720 to 2359296 took 0.07829999923706055 msecs. Success: true
fxRunID 0
Metering callback: 10005
Aborted(alignment fault)
file:///C:/Projects/js-snapshotting/dist/mylib.mjs:560
 /** @suppress {checkTypes} */ var e = new WebAssembly.RuntimeError(what);
                                       ^

RuntimeError: Aborted(alignment fault)
    at abort (file:///C:/Projects/js-snapshotting/dist/mylib.mjs:560:40)
    at alignfault (file:///C:/Projects/js-snapshotting/dist/mylib.mjs:347:2)
    at mylib.wasm.SAFE_HEAP_LOAD_i32_2_U_2 (wasm://wasm/mylib.wasm-0180dcee:wasm-function[2191]:0x349da4)
    at mylib.wasm.fxRunID (wasm://wasm/mylib.wasm-0180dcee:wasm-function[1537]:0x2bae2e)
    at invoke_viii (file:///C:/Projects/js-snapshotting/dist/mylib.mjs:4450:27)
    at mylib.wasm.fxRunScript (wasm://wasm/mylib.wasm-0180dcee:wasm-function[1568]:0x2cbc0b)
    at mylib.wasm.fx_eval (wasm://wasm/mylib.wasm-0180dcee:wasm-function[1006]:0x13ef7d)
    at invoke_vi (file:///C:/Projects/js-snapshotting/dist/mylib.mjs:4483:27)
    at mylib.wasm.fxRunID (wasm://wasm/mylib.wasm-0180dcee:wasm-function[1537]:0x25e562)
    at mylib.wasm.fxRunCount (wasm://wasm/mylib.wasm-0180dcee:wasm-function[126]:0x1afc2)
```

And it actually tells me here that it's while it's in `fxRunID` while executing the script. I don't know if this is the same cause as the other failure, but it's worth considering.

I'm not sure why, but when I run it in the browser then the second `fxRunID` is called `$fxRunID` and it doesn't give me the source location of the issue.

I added an instruction counter inside `fxRunID` and I'm surprised it only gets to a count of 2 before failing. I suppose that's a good thing.

It seems to be stopping on instruction code 83, which is apparently `XS_CODE_FILE`.

It's incredibly slow to step.

Possibly the reason why I don't have debug location information is because of the way switch and case is implemented.

---------

2023-12-06 06:46 I swapped out the implementation of `switch` and `case` in the hope that I'd get debug information again, but it doesn't seem so. So anyway, I know that the issue is triggered in the `XS_CODE_FILE` instruction. I'll just use print statements to isolate it further.

It appears to be happening right at the beginning of the instruction, in `count = mxRunID(1);`

Ok, interesting. There's a macro definition here:

```c
#define mxRunS1(OFFSET) ((txS1*)mxCode)[OFFSET]
#define mxRunU1(OFFSET) ((txU1*)mxCode)[OFFSET]
#if mxBigEndian
	#define mxRunS2(OFFSET) (((txS1*)mxCode)[OFFSET+0] << 8) | ((txU1*)mxCode)[OFFSET+1]
	#define mxRunS4(OFFSET) (((txS1*)mxCode)[OFFSET+0] << 24) | (((txU1*)mxCode)[OFFSET+1] << 16) | (((txU1*)mxCode)[OFFSET+2] << 8) | ((txU1*)mxCode)[OFFSET+3]
	#define mxRunU2(OFFSET) (((txU1*)mxCode)[OFFSET+0] << 8) | ((txU1*)mxCode)[OFFSET+1]
#else
	#if mxUnalignedAccess
		#define mxRunS2(OFFSET) *((txS2*)(mxCode + OFFSET))
		#define mxRunS4(OFFSET) *((txS4*)(mxCode + OFFSET))
		#define mxRunU2(OFFSET) *((txU2*)(mxCode + OFFSET))
	#else
		#define mxRunS2(OFFSET) (((txS1*)mxCode)[OFFSET+1] << 8) | ((txU1*)mxCode)[OFFSET+0]
		#define mxRunS4(OFFSET) (((txS1*)mxCode)[OFFSET+3] << 24) | (((txU1*)mxCode)[OFFSET+2] << 16) | (((txU1*)mxCode)[OFFSET+1] << 8) | ((txU1*)mxCode)[OFFSET+0]
		#define mxRunU2(OFFSET) (((txU1*)mxCode)[OFFSET+1] << 8) | ((txU1*)mxCode)[OFFSET+0]
	#endif
#endif
#ifdef mx32bitID
	#define mxRunID(OFFSET) mxRunS4(OFFSET)
#else
	#define mxRunID(OFFSET) mxRunS2(OFFSET)
#endif
```

According to some printf statements, the values currently used are these:

```
not mx32bitID
mxUnalignedAccess
not mxBigEndian
```

Ok, so that will need to be added to the port file, since I don't think WASM supports unaligned access. The direction of the definition of `mxUnalignedAccess` here seems to be that when its false, access must be aligned and so the code does extra steps to read the data in pieces.

Ok, so I've added `-DmxUnalignedAccess=0` to the makefile and I'm rebuilding everything.

Ok, that fixed the alignment problem, and the script appears to run to completion now, with all the emscripten checks in place. But I still get this error:

```
#
# Fatal error in , line 0
# Check failed: IdField::is_valid(id).
#
#
#
#FailureMessage Object: 000000AE3DDFE0C0
 1: 00007FF745DBAEFF node_api_throw_syntax_error+206127
 2: 00007FF745CD15DF v8::CTypeInfoBuilder<void>::Build+11951
 3: 00007FF746B39492 V8_Fatal+162
 4: 00007FF746B71F4A v8::internal::compiler::HeapConstantType::Value+666
 5: 00007FF746C257DA v8::internal::compiler::Graph::NewNodeUnchecked+58
 6: 00007FF746BCDDE2 v8::internal::compiler::MachineOperatorReducer::Int32Constant+6146
 7: 00007FF7462001DC v8::internal::wasm::BuildTFGraph+110092
 8: 00007FF7461E4D28 v8::internal::wasm::JumpTableAssembler::NopBytes+20904
 9: 00007FF7461E47CD v8::internal::wasm::JumpTableAssembler::NopBytes+19533
10: 00007FF7461E72DF v8::internal::wasm::BuildTFGraph+7951
11: 00007FF7461EB0BF v8::internal::wasm::BuildTFGraph+23791
12: 00007FF7461E62CD v8::internal::wasm::BuildTFGraph+3837
13: 00007FF7461E5589 v8::internal::wasm::BuildTFGraph+441
14: 00007FF746BCA027 v8::internal::compiler::CompileWasmImportCallWrapper+10615
15: 00007FF746201D1C v8::internal::wasm::WasmCompilationUnit::ExecuteFunctionCompilation+1324
16: 00007FF746201725 v8::internal::wasm::WasmCompilationUnit::ExecuteCompilation+229
17: 00007FF7461D5A8D v8::internal::wasm::CompileToNativeModule+4413
18: 00007FF7461DBA5E v8::internal::wasm::CompilationState::New+7662
19: 00007FF74694F260 v8::internal::SetupIsolateDelegate::SetupHeap+1437648
20: 00007FF745CD3FAD X509_STORE_CTX_get_lookup_certs+813
21: 00007FF745E0824D uv_poll_stop+557
22: 00007FF746DC6880 v8::internal::compiler::ToString+146448
23: 00007FFED0C87344 BaseThreadInitThunk+20
24: 00007FFED0DC26B1 RtlUserThreadStart+33
```

That's a tricky one. Let me see what I can learn from the above:

- `RtlUserThreadStart` probably tells us what thread the error is happening in. But I can't actually figure out if that's the main user thread or something else like a worker. Reading through the library that emscripten produces, I don't see anywhere that it creates a worker, so I have no reason to think that the user thread is anything but the main thread.

- `wasm::CompileToNativeModule` looks like it's an error while compiling WASM. Since the WASM has already run to completion, that's a bit strange. I wonder if `mylib.mjs` is doing some asynchronous loading.

- The last frame before `V8_Fatal` is `v8::internal::compiler::HeapConstantType::Value` which sounds like a pretty normal operation to me.

Honestly, it looks like a bug in node.js to me. I'm on node v18.8.0. Let me perhaps look at using node 20.

Actually, before I install that, let me do a little bit of debugging, because I would prefer this issue to be resolved on all host engines.

N-API documentation says that `node_api_throw_syntax_error` does "This API throws a JavaScript SyntaxError with the text provided."

https://nodejs.org/api/n-api.html#node_api_throw_syntax_error

Why is it compiling WASM after the program's already finished?

Let me see if I can use the debug stepping to find if anything runs on a timer or anything after the end of my script.

In the debugger in the browser of course I'm getting all sorts of crap running after the end of my script (chrome extensions). Maybe I'll try run in node in the debugger.

So on the last console.log line (`console.log('done');`) I put a breakpoint, and then repeatedly used "step-into" to see if anything came after it, but the debugger didn't stop again. But after a delay, I did see `Process exited with code 2147483651`.

I thought that that would step into any further code, but perhaps I'm mistaken because I added this before the end:

```js
setTimeout(() => {
  console.log('something else')
}, 1000);
```

and the debugger never got here.

So another strategy, I'm just going to look in `mylib.mjs` for anything that might schedule future work.

Ok, actually, I opened a guest browser window and did the step-into thing and I can see it step into the timeout just fine but then it doesn't step into anything else, so I think I'm fairly convinced that the library doesn't schedule any further work, at least on the browser. It's still possible that it does things differently in node.js.

Another thing that could be happening is that the GC is triggered after some time in node.js. I don't know why the GC would cause compilation of a WASM module, so it doesn't really make sense. But it is something that could be scheduled that is not a timer or promise. To test this theory, I'm going to see if I can set a timer to call the test routine again later. This should at least hold the wasm module in memory and prevent it from being collected. The failure is normally 10 seconds after completion, so I'll do this with a 20s timeout.

Ok, that didn't work.

Now I'm going to try with the `testProcessMessage` only in the timer. The theory this tests is that it's the module initialization itself which starts the 10 second self-destruct.

Ok, what happened is that it took 20 seconds before running `testProcessMessage` and then another 10 seconds before dying.

Ok, finally I'm going to just upgrade to node 20.10.0 (x64) and see if that changes/fixes anything.

2023-12-06 07:44 Ok, I have node v20.10.0 installed. Fingers crossed.

Ok, interesting behavior. It doesn't crash, but it also isn't ending the program. I assume that I need to call some kind of close method.

Oh, it did eventually stop on its own. I assume the GC is kicking in. Or did I cancel the process unintentionally? Let me run it again and this time I'll just wait.

Ah yeah it does seem to terminate on its own, but it takes a minute or two.

If I don't actually call `testProcessMessage` then it terminates fine on its own. I wonder what aspect of it is holding on to resources.

By the way, it looks like I can now step through the C code in node.js as well. It seems to support DWARF debugging, which it didn't before.

I think I'm going to set aside this lingering-lifetime issue for the moment. For this POC, there are some more important things I want to do:

1. How big is a snapshot?
2. Getting command execution working.

So I'm going to start by trying to pipe a snapshot to the host.

I'm getting a "stack overflow" while trying to take the snapshot. I expect it's the same kind of issue I had before, that the stack checking just doesn't work.

It seems to happen right near the beginning, when it runs a garbage collection.

Yeah, there's another `fxCheckCStack`. I'm just adding a return to it.

The beginning of the snapshot writing seems to be sensible, but then it trails off into writing the same address `0xfd6c` multiple times.

```
...
snapshotWrite 0 0x5b568 32
snapshotWrite 0 0x5b538 24
snapshotWrite 0 0x5b550 24
snapshotWrite 0 0x5be10 8
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
snapshotWrite 0 0xfd6c 4
```

Ah. xsnap uses it like this:

```c
static int fxSnapshotWrite(void* stream, void* address, size_t size)
{
	SnapshotStream* snapshotStream = stream;
	size_t written = fwrite(address, size, 1, snapshotStream->file);
	snapshotStream->size += size * written;
	return (written == 1) ? 0 : errno;
}
```

So based on the arguments of `fwrite`, it seems like `address` is actually a pointer to the data not the target address. So when we're reading from the same address repeatedly, it's probably an intermediate buffer of local variable.

So I assume that the snapshotting process is designed to output linearly only.

Anyway, I have enough to calculate the size now.

Total output size 87433.

That's hilariously big for what is essentially an empty VM. And it's hilarious that the smallest snapshot size is much bigger than Microvium's maximum memory capacity.

But still its small enough for my purposes. But it's informative. It definitely requires some kind of file storage.

I'm curious whether any of the machine creation parameters affect this. I'll multiply everything by 2 and see if it changes the snapshot size. No, it's identical. So at least I know it's not snapshotting unused memory.

Ok, so next I'll need to write some logic to create a self-expanding buffer to store the output, and then return it to JS, and send it back to C to restore a snapshot and see if I can get a round-trip working.

----

Actually, next I'm going to try fix the weird non-existing issue. The symptom is that the program stays open for much longer in node.js than the termination of the script. I'm going to remove pieces of the code until I figure out what might be causing it.

Ok, when I remove the script evaluation then it exits immediately.

I can see in the difference between the logs that the heap resizing is a bit different and also that the metering is triggered.

I wonder if it's something related to the debugging. Maybe it's trying to establish a debug network connection or something. Here are some possible ideas:

1. The VM is setting up a network socket for debugging.
2. The VM is setting up another thread.

I'm going to turn off debugging and metering and see what it does.

Ok, it still hangs. Next I'm going to set these to zero:

```c
#define mxUseGCCAtomics 0
#define mxUsePOSIXThreads 0
```

It still hangs.

Maybe I'll try performing the parse and evaluation separately.

Btw, `eval` seems to indirectly invoke `fx_eval`.

Apparently the parsing on its own doesn't cause the freezing.

When it runs the parsed script, it does so with `fxRunScript`. It's a bit of a long shot but I'm going to try early exit from `fxRunScript` and see at what point it causes the freeze.

Perhaps not surprisingly, it's the line `mxRunCount(0);` that seems to be causing the freeze. I'll have to see what opcodes get executed there.

These are the only opcodes:

```
Opcode 11
Opcode 83
Opcode 124
Line 1
Opcode 75
Opcode 201
Opcode 201
Opcode 1
Opcode 201
Opcode 1
Opcode 187
Opcode 169
```

I'm going to just do a binary search here.

Interesting. It's only when it invokes `XS_CODE_LINE` (124) that it causes the freeze. If I return from `fxRunID` before processing that instruction then the environment terminates fine.

Ok, it's more complicated than that. If I put the return *after* XS_CODE_LINE then it also terminates fine.

That's weird. If I put the return at the beginning of `XS_CODE_EVAL_ENVIRONMENT` (75), it doesn't terminate ok. But the beginning of XS_CODE_EVAL_ENVIRONMENT is basically meant to be the same as the end of XS_CODE_LINE.

If I comment out the body of XS_CODE_LINE, I still get the same freezing behavior. So it's not something specifically related to the implementation of that opcode.

I wonder if it could be something like a stack overflow. Or it could be the metering. Nah, the metering is just incrementing a counter, at least in this part of the loop.

I'll check if it's something memory-related by bumping up the initial memory so it doesn't need to resize.

I bumped up all the memory -- 4x for the creation parameters and 8x for the VM memory. I can see that it doesn't go through any resizing, but it still hangs at the end of the script.

Ok, I think I give up. The problem is reasonably benign, and it doesn't show up in the browser.

Actually one more thing, I'm going to read through all the emscripten options and see if I can lock it down more. If the issue is related to a thread or something weird like that, then maybe adding debug output or disabling features may help.

The issue didn't go away, but with extra logging:

```
Initializing wasm module...
asynchronously preparing wasm
run() called, but dependencies remain, so not running
initRuntime
Calling exported function...
syscall! fd_write: [1,129904,2,129900]
Size of xsSlot: 16
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
creating machine...
syscall return: 0
syscall! fd_write: [1,129584,2,129580]
Size of txSlot: 16
syscall return: 0
syscall! fd_write: [1,129568,2,129564]
fxAllocateSlots: 4096 * 16
syscall return: 0
syscall! fd_write: [1,129536,2,129532]
fxAllocateSlots: 32768 * 16
syscall return: 0
growMemory: 2097152 (+1048576 bytes / 16.999984741210938 pages)
Heap resize call from 1048576 to 2097152 took 0.5631999969482422 msecs. Success: true
syscall! fd_write: [1,129904,2,129900]
begin metering...
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
begin host...
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
host vars...
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
begin try...
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
Parsing script
syscall return: 0
growMemory: 4194304 (+2097152 bytes / 32.99998474121094 pages)
Heap resize call from 2097152 to 4194304 took 2.487799882888794 msecs. Success: true
syscall! fd_write: [1,129904,2,129900]
Script prepared for eval
syscall return: 0
syscall! fd_write: [1,126560,2,126556]
Metering callback: 10005
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 11
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 83
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 124
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 75
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
XS_CODE_EVAL_ENVIRONMENT
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 201
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 201
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 1
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 201
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 1
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 187
syscall return: 0
syscall! fd_write: [1,126608,2,126604]
Opcode 169
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
Eval called
syscall return: 0
syscall! fd_write: [1,129904,2,129900]
end metering...
syscall return: 0
reading output
freeing memory
Hello, World
syscall! fd_write: [1,130688,2,130684]
take_snapshot
syscall return: 0
syscall! fd_write: [1,130688,2,130684]
totalSize: 87433
syscall return: 0
done
```

I don't see anything here that would be a problem.

So yeah, I think I'm giving up on this one for the moment.

2023-12-09 06:17 Oh, in the task manager, I can see an instance of node.js taking up GB of RAM. And I added a `process.exit(0);` to the end of the script and it didn't actually exit.

Oh this is also weird: in task manager, the "command line" for all my node instances includes Adobe Creative Cloud in the path. Oh, I think that's a red herring. It's because when I'm running under the debugger, the "actual" node process doesn't show up in the task manager under the "processes" window. Maybe it's spun up as a child process or something.

When running in the debugger, I don't get the same weird behavior. The memory doesn't spike. It seems to sit around 20 MB before running the VM. Then it goes up to 24MB when I create the VM.

2023-12-09 06:41 Ok, so actually I do still get some weird spiking while running in the debugger, but it seems to get released again immediately. But it does go up to a GB. It does appear to be when eval is called.

When running outside the debugger, the memory spikes to more like 6GB, and then freezes while it tries to release that memory.

I'm going to try do some memory profiling with `node --inspect-brk run.mjs`.

----

Parsing script
Grow memory
Script prepared for eval
!Spike
Metering callback: 10005

Apparently no opcodes executed before the spike.

Ok, interesting. I added a log to `fx_eval` and it appears that the spike happens before the entry to `fx_eval`.

Added more logging. The spike happens between fxRunCount and fxRunID.

That's weird as heck, because `fxRunCount` doesn't do anything except call `fxRunID`

```c
void fxRunCount(txMachine* the, txInteger count)
{
	printf("fxRunCount\n");
	fxRunID(the, C_NULL, count);
}
```

Another way of putting it is that it's something in prolog of fxRunID that's actually causing the issue.

Now I'm having issues getting into the inspector.

I can reproduce the issue in the VS code debugger. If I breakpoint on fxRunCount and then step into fxRunID, the process of stepping into it causes the memory to spike.

Ok, I managed to get the inspector going again. The thing is, when I step out of the syscall hook, which should be stepping back to `fxRunCount`, I see the spike. It's around a GB.

So I have another theory as to why I'm seeing this behavior: it's the debugger or something related to debugging.

This explains why it spikes more (6GB) in VS Code than in inspector (1GB), since inspector doesn't have the debug symbols. But inspector is still loading something so that it can render the disassembly.

Running on this theory, I'm going to try remove the compilation of DWARF and debug symbols.

Urg. It was a good theory but I still have the same problem.

Now I'm trying with O2 optimization and bumping up the stack memory to a MB (in case the issue is that fxRunID is just doing something crazy with the stack). I need to bump up the total initial memory as well to handle the larger stack.

Ok, that actually worked. I'm guessing it's the optimization, but let me restore the smaller stack and memory size and see what happens.

Yeah ok, it was the optimization that helped, because it still works fine when I reduce the stack and initial memory back down. Although I think it's not a bad idea to have quite a large stack here, so I might bump it back up again.

I don't have a satisfactory theory about why it's behaving like this. I understand that `fxRunID` is a big function. Maybe there's a bug in the WASM runtime that it can't handle particularly big functions very well.

Anyway, I'm pleased that it's working. Next I'll need to clean up. I want to re-enable various safety checks, and I want to remove all the extra print statements and logging that I put in.

-----

2023-12-09 10:45 Ok, I think I'm finally ready to start developing the API properly. What do I need:

1. A way to construct an empty VM.
2. A way to run a script in a VM.
3. A way to pass a message to the VM.
4. A way to get a message from the VM.
5. A way to snapshot a VM.
6. A way to restore a VM from a snapshot.

-------

2023-12-12 07:51 I'm having a ton of trouble getting the build pipeline to work. It's quite a tall order to build ESM together with TypeScript and getting ESM and CommonJS out.

Ok, it's a bit of a mess but it seems to be working:

- Library is written in modular typescript (.mts)
- Wasm wrapper is modular JavaScript (.mjs)
- Unit tests are in CommonJS TypeScript (.ts)
- The build process copies the wrapper into the `src` folder, but I've git-ignored it.
- Rollup gives an unresolved dependency warning but the output seems to work ok anyway and as far as I can tell it's got everything in it.

----

I was hoping to implement a `sendMessage` host function by just getting it to invoke a JS function. But it seems like that's not very easy to do, so I'm instead going back to the single `process_message` function. I can still expose a `sendMessage` to the guest JavaScript, but it will have to accumulate into a queue of messages to be sent. Then the result of `process_message` will somehow reference the bundle of messages. Given that I know that they're JSON-encoded, I could just have a single output buffer which I append `[`, `,`, and `]` to separate the messages, and expand as necessary.

----

2023-12-13 07:29 Actually, I'm going to take another shot at trying to figure out if I can call a JavaScript function from C, because after thinking about it, the buffering of messages would not be intuitive and may have an effect on debugging or in the case of infinite loops, etc.

Ah, I found the section in the docs: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#implement-a-c-api-in-javascript

Ok, I figured out the way to do it.