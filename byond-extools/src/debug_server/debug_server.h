#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../core/socket/socket.h"
#include "protocol.h"

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
	SocketServer debugger;

public:
	bool connect();
	void debug_loop();
};


bool debugger_initialize();
bool debugger_connect();