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
	STEP_OUT,
	RESUME
};

enum class StepMode
{
	NONE,
	PRE_INTO,
	INTO,
	PRE_OVER,
	PAUSE,
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
	std::uint32_t step_over_sequence_number = UINT32_MAX;
	std::uint32_t step_over_parent_sequence_number = UINT32_MAX;
	std::optional<Breakpoint> breakpoint_to_restore = {};

	std::unordered_map<int, std::unordered_map<int, Breakpoint>> breakpoints;
	std::unordered_map<unsigned int, std::unordered_multimap<unsigned int, unsigned int>> data_breakpoints_read;

	void set_breakpoint(int proc_id, int offset, bool singleshot=false);
	std::optional<Breakpoint> get_breakpoint(int proc_id, int offset);
	void remove_breakpoint(int proc_id, int offset);
	void restore_breakpoint();

	enum class HandleMessageResult;

	bool connect(const char* port);
	bool listen(const char* port);
	HandleMessageResult handle_one_message();
	bool loop_until_configured();
	void debug_loop();

	NextAction wait_for_action();

	void on_error(ExecutionContext* ctx, const char* error);
	void on_breakpoint(ExecutionContext* ctx);
	void on_step(ExecutionContext* ctx, const char* reason = "step");
	void on_break(ExecutionContext* ctx);
	void on_data_breakpoint(ExecutionContext* ctx, unsigned int type, unsigned int value, std::string var_name);

	void send_simple(std::string message_type);
	void send(std::string message_type, nlohmann::json content);
	void send_call_stacks(ExecutionContext* ctx);
};


bool debugger_initialize();
bool debugger_enable(const char* mode, const char* port);
