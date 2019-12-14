#pragma once

/*

All communication happens over a TCP socket using a JSON-based protocol. A null byte signifies the end of a message.

struct Message
{
	std::string type;
	<various> content;
};

struct DisassembledProc
{
	std::string name;
	int override_id;
	std::vector<DisassembledInstruction> instructions;
};

struct DisassembledInstruction
{
	int offset;
	std::string bytes;
	std::string mnemonic;
	std::string comment;
	std::vector<unsigned short> possible_jumps;
	std::vector<std::string> extra;
};

struct BreakpointHit
{
	std::string proc;
	int override_id;
	int offset;
};

struct BreakpointSet
{
	std::string proc;
	int override_id;
	int offset;
};

struct BreakpointUnset
{
	std::string proc;
	int override_id;
	int offset;
};

struct ValueText
{
	std::string type;
	std::string value;
};

struct ProcListEntry
{
	std::string name;
	int override_id;
};

struct StackFrame
{
	std::string name;
	int override_id;
	ValueText usr;
	ValueText src;
	std::vector<ValueText> locals;
	std::vector<ValueText> args;
	int instruction_pointer;
};

struct FieldRequest
{
	std::string datum_type; //datum_type may for example be "DATUM" or "OBJ" or "MOB"
	int datum_id;
	std::string field_name;
};

struct Datum
{
	std::string datum_type;
	int datum_id;
};

struct Runtime //Similar to breakpoints, followed by call stack and scope messages
{
	std::string name;
	int override_id;
	int offset; //this and other groups of variables appear several times in the protocol, probably should collect them into structs at some point
	std::string message //Just the error message and no stack trace etc. For example "Division by zero"
};

struct ListContents
{
	bool is_assoc;
	std::vector<ValueText> OR std::vector<std::pair<ValueText, ValueText>> elements;
};

struct ProfileTime
{
	int seconds;
	int microseconds;
}

struct ProfileEntry
{
	std::string name;
	ProfileTime self;
	ProfileTime total;
	ProfileTime real;
	ProfileTime overtime;
	int call_count;
};

struct VariableRead
{
	std::string datum_type;
	int datum_id;
	std::string variable_name;
};

struct VariableWrite
{
	std::string datum_type;
	int datum_id;
	std::string variable_name;
	ValueText new_value;
};

*/

#define MESSAGE_RAW "raw message" //Content is a string, used for debugging purposes (how meta)
#define MESSAGE_PROC_LIST "proc list" // Content is a vector of ProcListEntry.
#define MESSAGE_PROC_DISASSEMBLY "proc disassembly" //Request content is the proc name, response content is DisassembledProc
#define MESSAGE_BREAKPOINT_HIT "breakpoint hit" //Content is BreakpointHit
#define MESSAGE_BREAKPOINT_SET "breakpoint set" //Content is BreakpointSet
#define MESSAGE_BREAKPOINT_UNSET "breakpoint unset" //Content is BreakpointUnset
#define MESSAGE_BREAKPOINT_STEP_INTO "breakpoint step into" //Content is empty
#define MESSAGE_BREAKPOINT_STEP_OVER "breakpoint step over" //Content is empty
#define MESSAGE_BREAKPOINT_RESUME "breakpoint resume" //Content is empty
#define MESSAGE_BREAKPOINT_PAUSE "breakpoint pause"
#define MESSAGE_VALUES_LOCALS "locals" //Content is a vector of ValueTexts
#define MESSAGE_VALUES_ARGS "args" //^
#define MESSAGE_VALUES_STACK "stack" //^
#define MESSAGE_CALL_STACK "call stack" //Content is a vector of StackFrames
#define MESSAGE_GET_FIELD "get field" //Request content is FieldRequest, response content is ValueText
#define MESSAGE_GET_GLOBAL "get global" //Request content is a string with the global name, response is a ValueText
#define MESSAGE_GET_TYPE "get type" //Request content is Datum, response content is a string
#define MESSAGE_RUNTIME "runtime" //Content is a Runtime
#define MESSAGE_TOGGLE_BREAK_ON_RUNTIME "break on runtimes" //Request content is true or false
#define MESSAGE_GET_LIST_CONTENTS "get list contents" //Request content is a list id, response content is ListContents;
#define MESSAGE_GET_PROFILE "get profile" //Request content is the proc name, response content is ProfileEntry
#define MESSAGE_ENABLE_PROFILER "enable profiler" //Request content is empty
#define MESSAGE_DISABLE_PROFILER "disable profiler" //Request content is empty
#define MESSAGE_GET_ALL_FIELDS "get all fields" //Request content is a Datum, response is a map<std::string, ValueText>
#define MESSAGE_DATA_BREAKPOINT_READ "data breakpoint read" //Content is a VariableRead
#define MESSAGE_DATA_BREAKPOINT_WRITE "data breakpoint write" //Content is a VariableWrite
#define MESSAGE_CONFIGURATION_DONE "configuration done"
