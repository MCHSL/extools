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
	std::vector<DisassembledInstruction> instructions;
};

struct DisassembledInstruction
{
	int offset;
	std::string bytes;
	std::string mnemonic;
	std::string comment;
};

struct BreakpointHit
{
	std::string proc;
	int offset;
};

struct BreakpointSet

*/

#define MESSAGE_RAW "raw message" //Content is a string, used for debugging purposes (how meta)
#define MESSAGE_PROC_LIST "proc list" // Content is a vector of proc paths.
#define MESSAGE_PROC_DISASSEMBLY "proc disassembly" //Request content is the proc name, response content is DisassembledProc
#define MESSAGE_BREAKPOINT_HIT "breakpoint hit" //Content is BreakpointHit