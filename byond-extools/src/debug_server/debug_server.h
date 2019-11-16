#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../core/socket/socket.h"
#include "protocol.h"

struct Breakpoint
{
	Core::Proc proc;

	int replaced_opcode;
	int offset;

	bool one_shot;
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