# BYOND extools
External tools for BYOND. Used to fiddle with the game engine's internals.

<table>
<tr><td align="right" valign="middle"><a href="https://ci.appveyor.com/project/MCHSL/extools"><img src="https://ci.appveyor.com/api/projects/status/github/MCHSL/extools?svg=true" alt="Appveyor status" /></a></td><td>Windows build (with <a href="https://ci.appveyor.com/project/MCHSL/extools/build/artifacts">binaries</a>)</td></tr>
<tr><td align="right" valign="middle"><a href="https://travis-ci.org/MCHSL/extools"><img src="https://travis-ci.org/MCHSL/extools.svg?branch=master" alt="Travis status" /></td><td>Linux build (work in progress)</td></tr>
</table>

## Isn't there a project just like this already?
Most of development of [Lunar-Dreamland](https://github.com/goonstation/Lunar-Dreamland) has shifted from Lua to C++. I got tired of having to interface between them, so I decided to go with the latter.

## What can I do with it?
Here are the modules currently available (not counting the core). Scroll to the bottom to see install instructions.

#### Proc hooking
Not exactly a module, but still useful. Hooking allows you to reimplement procs in C++, which usually run much faster, especially if the hooked proc is, for example, just math operations.

#### Debug server
Interfaces with debugger frontends, providing various information and managing breakpoints.  
[SpacemanDMM](https://github.com/SpaceManiac/SpacemanDMM) by SpaceManiac enables line-by-line debugging and live viewing of variables. More to come.  
Steamport's [Somnium](https://github.com/steamp0rt/somnium) is currently non-functional, but will allow debugging of low-level bytecode.  

#### TFFI
Threaded FFI for BYOND. Automagically threads off all DLL calls and prevents them from locking up the game until they return. You may use a Promise datum, pass a callback (global or an object) or simply sleep until the call returns.

#### Extended Profiler
Generates an in-depth analysis of proc performance. Records all procs called by the profiled proc and outputs a detailed breakdown of execution time, with nanosecond accuracy. Use https://www.speedscope.app/ to visualize the results.

Call `initialize_profiling()` and then use `start_profiling(/some/proc/path)` to begin profiling. Each time the proc is called will be recorded to a file in the `profiling` directory next to the .dmb file. Call `stop_profiling(/some/proc/path)` to stop new profiles from being created.

Known issues:

- Procs with sleeps in them may behave oddly and cause corrupted results.
- Spawn()s are entirely untested and may not work at all.
- The files containing profile results can become extremely large, in the range of gigabytes, preventing speedscope from importing them. Be careful when profiling procs that could run for longer than several seconds.

#### Optimizer
Currently a proof of concept. The only optimization available is inlining - the optimizer will go through all procs and attempt to inline global proc calls to eliminate call overhead. At the time of writing optimizing takes an incredibly long time to finish, which makes it infeasible to use.

#### Maptick
Hooks an internal BYOND function which sends map updates to players, and measures time taken to execute it. The result is written to a global variable called `internal_tick_usage`. SS13 servers can use this information to adjust how much of a tick running subsystems can take, which reduces lag spikes perceived by players.

#### Sockets
Implements a TCP socket API similar to python's `socket`. Users may create new `/datum/socket`s, `connect()` them to a specified address and port, then `send()` and `recv()` strings. `recv()` sleeps without locking up the server until it receives any data.

#### `Topic()` filter
Replaces BYOND's malfunctioning `world/Topic()` spam limiter. You may create a white- and blacklist for always allowed and always denied IPs. Rogue clients who send data too quickly, except if they are on the whitelist, are automatically blacklisted until server restart.

## What will I be able to do with this?
These modules are planned to be included in the future.

- ~~Disasm: Disassembles procs into bytecode, mostly for use by other modules.~~
- ~~Debug server: For use with debugging; Manages breakpoints, sends and receives data from debuggers.~~
- Hotpatch server: Receives compiled bytecode (eg. from the [VSCode extension](https://github.com/SpaceManiac/SpacemanDMM)) and patches it in for live code replacement.
- ~~Maptick: Measures time taken by BYOND's SendMaps() function and makes it accessible from DM code, helping reduce lag spikes from not leaving enough processing time.~~
- Proxy objects: Forward variable reads and writes to C++.
- Websockets: Send and receive data using the websocket protocol.
- Lua: Allows writing lua scripts that replace builtin procs. Mostly for messing about.
- ~~Optimizer: Optimizes bytecode and inlines procs into each other to avoid call overhead.~~

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
- SpaceManiac

Let me know if I forgot to include you!

## Want to see more?
Feel free to [buy me a beer.](https://ko-fi.com/asd1337)
