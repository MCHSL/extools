// Extools protocol documentation.

// ----------------------------------------------------------------------------
// Extools data structures

interface ProcId {
    // Includes type and name, but not "proc" or "verb" keywords.
    // ex. "/pickweight" or "/client/New"
    proc: string,
    // Starts at 0.
    override_id: number,
}

interface ProcOffset extends ProcId {
    // Corresponds to "offset" field of DisassembledInstruction.
    offset: number,
}

interface DisassembledProc extends ProcId {
    instructions: DisassembledInstruction[],
}

interface DisassembledInstruction {
    offset: number,
    bytes: string,
    mnemonic: string,
    comment: string,
    possible_jumps: number[],
    extra: string[],
}

interface StackFrame extends ProcOffset {
    usr: Value,
    src: Value,
    dot: Value,
    locals: Value[],
    args: Value[],
}

interface Runtime extends ProcOffset {
    message: string,
}

interface BreakpointHit extends ProcOffset {
    reason: 'breakpoint opcode' | 'step';
}

interface ReadBreakpointHit {
    ref: Ref,
    field_name: string,
}

interface WriteBreakpointHit extends ReadBreakpointHit {
    new_value: Value,
}

// ----------------------------------------------------------------------------
// Profiling structures

interface ProfileTime {
    seconds: number,  // int
    microseconds: number,  // int
}

interface ProfileEntry extends ProcId {
    self: ProfileTime,
    total: ProfileTime,
    real: ProfileTime,
    overtime: ProfileTime,
    call_count: number,
}

// ----------------------------------------------------------------------------
// BYOND value types

// Logically a u32. High 8 bits are "kind". Low 24 bits are ID. Null is 0.
type Ref = number;

// A single literal value. Used when setting variables.
type Literal =
    { ref: Ref } |
    { number: number } |
    { string: string } |
    { typepath: string } |
    { resource: string } |
    { proc: string };
    // TODO: pops

// A literal with additional applicable information. Returned when retrieving
// variables in all contexts.
interface Value {
    literal: Literal,

    // True if "get fields", "get all fields" will work.
    has_vars?: boolean,
    // True if "get list contents" will work.
    is_list?: boolean,
}

// The contents of a list.
type ListContents =
    { linear: Value[] } |
    { associative: [Value, Value][] };

// ----------------------------------------------------------------------------
// Message declarations
// "request" indicates frontend -> extools
// "response" indicates extools -> frontend

interface Message {
    type: string,
    content: any,
}

interface MessageDeclarations {
    // requests and request + response pairs
    "raw message": {
        request: string,
        response: string,
    },
    "proc list": {
        request: {},
        response: ProcId[],
    },
    "proc disassembly": {
        request: ProcId,
        response: DisassembledProc,
    },
    "breakpoint set": {
        request: ProcOffset,
        response: ProcOffset,
    },
    "breakpoint unset": {
        request: ProcOffset,
        response: ProcOffset,
    },
    "breakpoint step into": {
        request: {},
    },
    "breakpoint step over": {
        request: {},
    },
    "breakpoint step out": {
        request: {}
    },
    "breakpoint resume": {
        request: {},
    },
    "breakpoint pause": {
        request: {},
    },
    "get field": {
        request: {
            ref: Ref,
            field_name: string,
        },
        response: Value,
    },
    "get all fields": {
        request: Ref,
        response: {
            [field_name: string]: Value,
        },
    },
    "get global": {
        request: string,
        response: Value,
    },
    "break on runtimes": {
        request: boolean,
        response: boolean,
    },
    "configuration done": {
        request: {},
    },
    "get list contents": {
        request: Ref,
        response: ListContents,
    },
    "get profile": {
        request: ProcId,
        response: ProfileEntry,
    },
    "toggle profiler": {
        request: boolean,
        response: boolean,
    },
    "get source": {
        request: 'stddef.dm',
        response: string,
    },
    // response only
    "breakpoint hit": {
        response: BreakpointHit,
    },
    "data breakpoint read": {
        response: ReadBreakpointHit,
    },
    "data breakpoint write": {
        response: WriteBreakpointHit,
    },
    "call stack": {
        response: {
            "current": StackFrame[],
            "suspended": StackFrame[][],
        },
    },
    "runtime": {
        response: Runtime,
    },
}
