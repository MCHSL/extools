# BYOND extools
External tools for BYOND. Used to fiddle with the game engine's internals.

## Isn't there a project just like this already?
Most of development of [Lunar-Dreamland](https://github.com/goonstation/Lunar-Dreamland) has shifted from Lua to C++. I got tired of having to interface between them, so I decided to go with the latter.

## What can I do with it?
Here are the modules currently available (not counting the core). Scroll to the bottom to see install instructions.

#### TFFI
Threaded FFI for BYOND. Automagically threads off all DLL calls and prevents them from locking up the game until they return. You may use a Promise datum, pass a callback or simply sleep until the call returns.

Calls the do_work function from sample.dll with 3 arguments. The proc sleeps until do_work returns.
```
var/result = call_wait("sample.dll", "do_work", "arg1", "arg2", "arg3")
```



Calls do_work with 1 argument. Returns a promise object. Runs some other code before calling P.resolve() to obtain the result.
```	
var/datum/promise/P = call_async("sample.dll", "do_work", "arg1")
... do something else ...
var/result = P.resolve()
```

Calls do_work with 2 arguments. The callback is invoked with the result as the single argument. Execution resumes immediately.
```
/proc/print_result(result)
	world << result

call_cb("sample.dll", "do_work", /proc/print_result, "arg1", "arg2")
```

## What will I be able to do with this?
These modules are planned to be included in the future.

- Disasm: Disassembles procs into bytecode, mostly for use by other modules.
- Debug server: For use with debugging; Manages breakpoints, sends and receives data from debuggers.
- Hotpatch server: Receives compiled bytecode (eg. from the [VSCode extension](https://github.com/SpaceManiac/SpacemanDMM)) and patches it in for live code replacement.
- Maptick: Measures time taken by BYOND's SendMaps() function and makes it accessible from DM code, helping reduce lag spikes from not leaving enough processing time.
- Lua: Allows writing lua scripts that replace builtin procs. Mostly for messing about.

## I want to use this!
Download the DLL and .dm file from [Releases](https://github.com/MCHSL/extools/releases). Place the DLL next to your DMB and plop the .dm somewhere where you can easily tick it. Afterwards, add `extools_initialize()` to `world/New()` or equivalent. To load modules, call `<module>_initialize()`, for example `tffi_initialize()`. Module initialization functions must be called after `extools_initialize()`!


## Credits
Thank you to people who contributed in one way or another to the overall effort.

- Somepotato
- ThatLing
- Steamport
- Karma
- Tobba
- ACCount
- Voidsploit
- Canvas123
- N3X1S
- mloc

Let me know if I forgot to include you!
