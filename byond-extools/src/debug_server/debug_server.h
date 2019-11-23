#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../core/socket/socket.h"
#include "protocol.h"

#include <condition_variable>
#include <mutex>

#define DEBUG_WAIT 0
#define DEBUG_STEP 1
#define DEBUG_RESUME 2

struct Breakpoint
{
	Core::Proc proc;

	std::uint32_t replaced_opcode;
	std::uint16_t offset;

	bool one_shot;

	bool operator==(const Breakpoint& rhs)
	{
		return proc == rhs.proc &&
				replaced_opcode == rhs.replaced_opcode &&
				offset == rhs.offset &&
				one_shot == rhs.one_shot;
	}
};

struct BreakpointRestorer
{
	std::uint32_t replaced_opcode;
	std::uint16_t offset_to_replace;
	std::uint16_t my_offset;

	bool operator==(const BreakpointRestorer& rhs)
	{
		return replaced_opcode == rhs.replaced_opcode &&
			offset_to_replace == rhs.offset_to_replace &&
			my_offset == rhs.my_offset;
	}
};

class DebugServer
{
	SocketServer debugger;
public:
	int next_action = DEBUG_WAIT;
	bool connect();
	void debug_loop();

	int wait_for_action();

	void send_simple(std::string message_type);
	void send(std::string message_type, nlohmann::json content);
};

extern std::uint32_t breakpoint_opcode;
extern std::uint32_t nop_opcode;
extern std::uint32_t singlestep_opcode;

extern std::unordered_map<unsigned short, std::vector<Breakpoint>> breakpoints;
extern std::unordered_map<unsigned short, std::vector<BreakpointRestorer>> singlesteps;
extern std::unique_ptr<Breakpoint> breakpoint_to_restore;

bool debugger_initialize();
bool debugger_connect();
Breakpoint set_breakpoint(Core::Proc proc, std::uint16_t offset, bool one_shot = false);
std::unique_ptr<Breakpoint> get_breakpoint(Core::Proc proc, int offset);