#include "../core/core.h"
#include "../dmdism/instruction.h"

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")

struct Breakpoint
{
	Core::Proc proc;
	Instruction replaced_instruction;
	bool one_shot;
	int instruction_index;
	int offset;
	int nop_count;
};

class DebugServer
{
	SOCKET debugger;

};


bool debugger_initialize();