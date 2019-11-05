# BYOND extools
External tools for BYOND. Used to fiddle with the game engine's internals.

## Isn't there a project just like this already?
Most of development of [Lunar-Dreamland](https://github.com/goonstation/Lunar-Dreamland) has shifted from Lua to C++. I got tired of having to interface between them, so I decided to go with the latter.

## What can I do with it?
Here are the modules currently available (not counting the core). Scroll to the bottom to see install instructions.

#### TFFI
Threaded FFI for BYOND. Automagically threads off all DLL calls and prevents them from locking up the game until they return. You may use a Promise datum, pass a callback (global or an object) or simply sleep until the call returns.

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

Calls do_work with 2 arguments. The callback, a global proc, is invoked with the result as the single argument. Execution resumes immediately.
```
/proc/print_result(result)
	world << result

call_cb("sample.dll", "do_work", GLOBAL_PROC, /proc/print_result, "arg1", "arg2")
```

Calls do_work with 3 arguments. The callback is invoked on the target object, with the result as the single argument. Execution resumes immediately.
```
/mob/proc/tell_result(result)
	src << result

call_cb("sample.dll", "do_work", mob, /mob.proc/print_result, "arg1", "arg2", "arg3")
```

#### Extended Profiler
Generates an in-depth analysis of proc performance. Records all procs called by the profiled proc and outputs a detailed breakdown of execution time, with microsecond accuracy. Use https://www.speedscope.app/ to visualize the results.

Call `initialize_profiling()` and then use `start_profiling(/some/proc/path)` to begin profiling. Each time the proc is called will be recorded to a file in the `profiling` directory next to the .dmb file. Call `stop_profiling(/some/proc/path)` to stop new profiles from being created.

Known issues:

- Procs with sleeps in them may behave oddly and cause corrupted results.
- Spawn()s are entirely untested and may not work at all.
- The files containing profile results can become extremely large, in the range of gigabytes, preventing speedscope from importing them. Be careful when profiling procs that could run for longer than several seconds.


#### Optimizer
Currently a proof of concept. The only optimization available is inlining - the optimizer will go through all procs and attempt to inline global proc calls to eliminate call overhead. At the time of writing crashes when attempting to optimize ss13, possibly because of being unable to disassemble procs due to missing opcodes.

## What will I be able to do with this?
These modules are planned to be included in the future.

- ~~Disasm: Disassembles procs into bytecode, mostly for use by other modules.~~
- Debug server: For use with debugging; Manages breakpoints, sends and receives data from debuggers.
- Hotpatch server: Receives compiled bytecode (eg. from the [VSCode extension](https://github.com/SpaceManiac/SpacemanDMM)) and patches it in for live code replacement.
- Maptick: Measures time taken by BYOND's SendMaps() function and makes it accessible from DM code, helping reduce lag spikes from not leaving enough processing time.
- Proxy objects: Forward variable reads and writes to C++.
- Websockets: Send and receive data using the websocket protocol.
- Lua: Allows writing lua scripts that replace builtin procs. Mostly for messing about.
- Optimizer: Optimizes bytecode and inlines procs into each other to avoid call overhead.

## I want to use this!
Download the DLL and .dm file from [Releases](https://github.com/MCHSL/extools/releases). Place the DLL next to your DMB and plop the .dm somewhere where you can easily tick it. Afterwards, add `extools_initialize()` to `world/New()` or equivalent. To load modules, call `<module>_initialize()`, for example `tffi_initialize()`. Module initialization functions must be called after `extools_initialize()`!

## How do I compile this?
You need [CMake](https://cmake.org/download/), at least version 3.15.  
### Windows
You need Visual Studio, preferably 2019. Be sure to include the "C++ CMake tools for Windows".  
Create a folder next to "byond-extools" called "build". Use this as the CMake "where to build your binaries" directory.  
You can use the CMake GUI. Ensure that you select **Win32**!  

![](https://i.imgur.com/4Sg9ECc.gif)

If you choose to use CMake from the command line:
```
D:\Code\C++\extools\byond-extools\build> cmake -G "Visual Studio 16 2019" -A Win32 ..
-- The C compiler identification is MSVC 19.23.28106.4
-- The CXX compiler identification is MSVC 19.23.28106.4
-- Check for working C compiler: C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/14.23.28105/bin/Hostx64/x86/cl.exe
-- Check for working C compiler: C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/14.23.28105/bin/Hostx64/x86/cl.exe -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Detecting C compile features
-- Detecting C compile features - done
-- Check for working CXX compiler: C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/14.23.28105/bin/Hostx64/x86/cl.exe
-- Check for working CXX compiler: C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/MSVC/14.23.28105/bin/Hostx64/x86/cl.exe -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done
-- Generating done
-- Build files have been written to: D:/Code/C++/extools/byond-extools/build
D:\Code\C++\extools\byond-extools\build>
```

## Linux
You can just make the build directory and do `cmake ..` and then `make`.  
32-bit is automatically forced when compiling on Linux.

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
