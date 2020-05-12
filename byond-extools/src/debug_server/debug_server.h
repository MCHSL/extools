#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../core/socket/socket.h"
#include "protocol.h"

#include <condition_variable>
#include <mutex>
#include <optional>

const char* const DBG_MODE_NONE = "NONE";
const char* const DBG_MODE_LAUNCHED = "LAUNCHED";
const char* const DBG_MODE_BACKGROUND = "BACKGROUND";
const char* const DBG_MODE_BLOCK = "BLOCK";

enum class NextAction
{
	WAIT,
	STEP_INTO,
	STEP_OVER,
	RESUME
};

enum class StepMode
{
	NONE,
	PRE_INTO,
	INTO,
	PRE_OVER,
	OVER
};

struct Breakpoint
{
	Core::Proc* proc;

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
	std::uint32_t breakpoint_replaced_opcode;
	std::uint32_t my_replaced_opcode;
	std::uint16_t offset_to_replace;
	std::uint16_t my_offset;

	bool operator==(const BreakpointRestorer& rhs)
	{
		return breakpoint_replaced_opcode == rhs.breakpoint_replaced_opcode &&
			offset_to_replace == rhs.offset_to_replace &&
			my_offset == rhs.my_offset;
	}
};

class DebugServer
{
	JsonStream debugger;
public:
	NextAction next_action = NextAction::WAIT;
	StepMode step_mode = StepMode::NONE;
	bool break_on_runtimes = false;
	ExecutionContext* step_over_context = nullptr;
	ExecutionContext* step_over_parent_context = nullptr;
	std::optional<Breakpoint> breakpoint_to_restore = {};

	std::unordered_map<int, std::unordered_map<int, Breakpoint>> breakpoints;

	void set_breakpoint(int proc_id, int offset, bool singleshot=false);
	std::optional<Breakpoint> get_breakpoint(int proc_id, int offset);
	void remove_breakpoint(int proc_id, int offset);
	void restore_breakpoint();

	bool connect(const char* port);
	bool listen(const char* port);
	int handle_one_message();
	bool loop_until_configured();
	void debug_loop();

	NextAction wait_for_action();

	void on_error(ExecutionContext* ctx, char* error);
	void on_breakpoint(ExecutionContext* ctx);
	void on_step(ExecutionContext* ctx);
	void on_break(ExecutionContext* ctx);

	void send_simple(std::string message_type);
	void send(std::string message_type, nlohmann::json content);
	void send_call_stack(ExecutionContext* ctx);
};


bool debugger_initialize();
bool debugger_enable(const char* mode, const char* port);
