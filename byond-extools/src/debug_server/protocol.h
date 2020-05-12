#pragma once

// The Extools debugger protocol consists of a null-separarated stream of JSON
// blobs. Each blob is a `Message` struct as described in `protocol.ts`.

// See `MessageDeclarations` in `protocol.ts` for the request-response
// structure of these message types.

// request only
#define MESSAGE_BREAKPOINT_STEP_INTO "breakpoint step into"
#define MESSAGE_BREAKPOINT_STEP_OVER "breakpoint step over"
#define MESSAGE_BREAKPOINT_RESUME "breakpoint resume"
#define MESSAGE_BREAKPOINT_PAUSE "breakpoint pause"
#define MESSAGE_CONFIGURATION_DONE "configuration done"

// request + response pair
#define MESSAGE_RAW "raw message"
#define MESSAGE_PROC_LIST "proc list"
#define MESSAGE_PROC_DISASSEMBLY "proc disassembly"
#define MESSAGE_BREAKPOINT_SET "breakpoint set"
#define MESSAGE_BREAKPOINT_UNSET "breakpoint unset"
#define MESSAGE_GET_FIELD "get field"
#define MESSAGE_GET_ALL_FIELDS "get all fields"
#define MESSAGE_GET_GLOBAL "get global"
#define MESSAGE_TOGGLE_BREAK_ON_RUNTIME "break on runtimes"
#define MESSAGE_TOGGLE_PROFILER "toggle profiler"
#define MESSAGE_GET_LIST_CONTENTS "get list contents"
#define MESSAGE_GET_PROFILE "get profile"
#define MESSAGE_GET_SOURCE "get source"

// response only
#define MESSAGE_BREAKPOINT_HIT "breakpoint hit"
#define MESSAGE_DATA_BREAKPOINT_READ "data breakpoint read"
#define MESSAGE_DATA_BREAKPOINT_WRITE "data breakpoint write"
#define MESSAGE_CALL_STACK "call stack"
#define MESSAGE_RUNTIME "runtime"
