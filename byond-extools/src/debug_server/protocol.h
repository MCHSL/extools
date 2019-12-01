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
	int datum_type;
	int datum_id;
	std::string field_name;
};

*/

#define MESSAGE_RAW "raw message" //Content is a string, used for debugging purposes (how meta)
#define MESSAGE_PROC_LIST "proc list" // Content is a vector of ProcListEntry.
#define MESSAGE_PROC_DISASSEMBLY "proc disassembly" //Request content is the proc name, response content is DisassembledProc
#define MESSAGE_BREAKPOINT_HIT "breakpoint hit" //Content is BreakpointHit
#define MESSAGE_BREAKPOINT_SET "breakpoint set" //Content is BreakpointSet
#define MESSAGE_BREAKPOINT_UNSET "breakpoint unset" //Content is BreakpointUnset
#define MESSAGE_BREAKPOINT_STEP "breakpoint step" //Content is empty
#define MESSAGE_BREAKPOINT_RESUME "breakpoint resume" //Content is empty
#define MESSAGE_VALUES_LOCALS "locals" //Content is a vector of ValueTexts
#define MESSAGE_VALUES_ARGS "args" //^
#define MESSAGE_VALUES_STACK "stack" //^
#define MESSAGE_CALL_STACK "call stack" //Content is a vector of StackFrames
#define MESSAGE_GET_FIELD "get field" //Request content is FieldRequest, response content is ValueText