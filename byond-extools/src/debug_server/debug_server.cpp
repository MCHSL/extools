#include "debug_server.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../dmdism/opcodes.h"
#include "../third_party/json.hpp"
#include <utility>
#include <unordered_map>

#include <thread>

std::uint32_t breakpoint_opcode;
std::uint32_t nop_opcode;

DebugServer debug_server;
std::mutex notifier_mutex;
std::condition_variable notifier;

RuntimePtr oRuntime;
//GetVariablePtr oGetVariable;

bool DebugServer::listen(const char* port)
{
	JsonListener listener;
	if (!listener.listen(port))
	{
		Core::Alert("couldn't listen");
		return false;
	}

	debugger = listener.accept();
	return true;
}

bool DebugServer::connect(const char* port)
{
	return debugger.connect(port);
}

void stripUnicode(std::string& str)
{
	str.erase(remove_if(str.begin(), str.end(), [](unsigned char c) {return !(c >= 0 && c < 128); }), str.end());
}

nlohmann::json value_to_text(Value val);

DataType datatype_name_to_val(std::string name)
{
	for (auto it = datatype_names.begin(); it != datatype_names.end(); ++it)
		if (it->second == name)
			return it->first;
	return DataType::NULL_D;
}

const int RES_BREAK = 0;
const int RES_CONTINUE = 1;
const int RES_CONFIGURATION_DONE = 2;

int DebugServer::handle_one_message()
{
	nlohmann::json data = debugger.recv_message();
	//Core::Alert("Message!!");
	if (data.is_null())
	{
		//Core::Alert("null message, leaving");
		return RES_BREAK;
	}
	const std::string& type = data.at("type");
	if (type == MESSAGE_RAW)
	{
		const std::string& echoing = data.at("content");
		Core::Alert("Echoing: " + echoing);
		debugger.send(MESSAGE_RAW, echoing);
	}
	else if (type == MESSAGE_PROC_LIST)
	{
		std::vector<nlohmann::json> procs;
		for (const Core::Proc& proc : Core::get_all_procs())
		{
			procs.push_back({ {"proc", proc.name}, {"override_id", proc.override_id} });
		}
		debugger.send(MESSAGE_PROC_LIST, procs);
	}
	else if (type == MESSAGE_PROC_DISASSEMBLY)
	{
		auto content = data.at("content");
		const std::string& proc_name = content.at("proc");
		int override_id = content.at("override_id");
		Core::Proc& proc = Core::get_proc(proc_name, override_id);
		Disassembly disassembly = proc.disassemble();
		nlohmann::json disassembled_proc;
		disassembled_proc["proc"] = proc_name;
		disassembled_proc["override_id"] = override_id;

		std::vector<nlohmann::json> instructions;
		for (Instruction& instr : disassembly.instructions)
		{
			std::string comment = instr.comment();
			stripUnicode(comment);
			nlohmann::json d_instr = {
				{ "offset", instr.offset() },
				{ "bytes", instr.bytes_str() },
				{ "mnemonic", instr.opcode().mnemonic() },
				{ "comment", comment },
				{ "possible_jumps", instr.jump_locations() },
				{ "extra", instr.extra_info() },
			};
			instructions.push_back(d_instr);
		}
		disassembled_proc["instructions"] = instructions;
		debugger.send(MESSAGE_PROC_DISASSEMBLY, disassembled_proc);
	}
	else if (type == MESSAGE_BREAKPOINT_SET)
	{
		//Core::Alert("BREAKPOINT_SET");
		auto content = data.at("content");
		const std::string& proc = content.at("proc");
		const int& override_id = content.at("override_id");
		//Core::Alert("Setting breakpoint in " + proc);
		set_breakpoint(Core::get_proc(proc, override_id).id, content.at("offset"));
		debugger.send(data);
	}
	else if (type == MESSAGE_BREAKPOINT_UNSET)
	{
		auto content = data.at("content");
		const std::string& proc = content.at("proc");
		const int& override_id = content.at("override_id");
		//Core::Alert("Setting breakpoint in " + proc);
		remove_breakpoint(Core::get_proc(proc, override_id).id, content.at("offset"));
		debugger.send(data);
	}
	else if (type == MESSAGE_BREAKPOINT_STEP_INTO)
	{
		std::lock_guard<std::mutex> lk(notifier_mutex);
		next_action = NextAction::STEP_INTO;
		notifier.notify_all();
	}
	else if (type == MESSAGE_BREAKPOINT_STEP_OVER)
	{
		std::lock_guard<std::mutex> lk(notifier_mutex);
		next_action = NextAction::STEP_OVER;
		notifier.notify_all();
	}
	else if (type == MESSAGE_BREAKPOINT_PAUSE)
	{
		debug_server.step_mode = StepMode::INTO;
	}
	else if (type == MESSAGE_BREAKPOINT_RESUME)
	{
		std::lock_guard<std::mutex> lk(notifier_mutex);
		next_action = NextAction::RESUME;
		notifier.notify_all();
	}
	else if (type == MESSAGE_GET_FIELD)
	{
		auto content = data.at("content");
		int ref = content.at("ref");
		data["content"] = value_to_text(Value((DataType)(ref >> 24), ref & 0xffffff).get_safe(content.at("field_name")));
		debugger.send(data);
	}
	else if (type == MESSAGE_GET_ALL_FIELDS)
	{
		int ref = data.at("content");
		Value datum = Value((DataType)(ref >> 24), ref & 0xffffff);
		nlohmann::json vals;
		for (const std::pair<std::string, Value>& v: datum.get_all_vars())
		{
			vals[v.first] = value_to_text(v.second);
		}
		data["content"] = vals;
		debugger.send(data);
	}
	else if (type == MESSAGE_GET_GLOBAL)
	{
		data["content"] = value_to_text(GetVariable(DataType::WORLD_D, 0x01, Core::GetStringId(data.at("content"))));
		debugger.send(data);
	}
	else if (type == MESSAGE_TOGGLE_BREAK_ON_RUNTIME)
	{
		break_on_runtimes = data.at("content"); //runtimes funtimes
		debugger.send(data);
	}
	else if (type == MESSAGE_GET_LIST_CONTENTS)
	{
		int ref = data.at("content");
		try {
			List list(ref & 0xffffff);

			std::vector<Value> elements = std::vector<Value>(list.list->vector_part, list.list->vector_part + list.list->length); //efficiency
			std::vector<nlohmann::json> textual;
			if (!list.is_assoc())
			{
				for (Value& val : elements)
				{
					textual.push_back(value_to_text(val));
				}
				data["content"] = { { "linear", textual } };
			}
			else
			{
				for (Value& val : elements)
				{
					textual.push_back(std::make_pair<nlohmann::json, nlohmann::json>(value_to_text(val), value_to_text(list.at(val))));
				}
				data["content"] = { { "associative", textual } };
			}
			debugger.send(data);
		}
		catch (const char* e) { //thrown by list constructor when trying to access an invalid list
			data["content"] = { {"linear", std::vector<nlohmann::json>()} };
			debugger.send(data);
		}
	}
	else if (type == MESSAGE_GET_SOURCE)
	{
		data["content"] = StdDefDM ? std::string(StdDefDM(nullptr)) : "";
		debugger.send(data);
	}

	else if (type == MESSAGE_GET_PROFILE)
	{
		const std::string& name = data.at("content");
		Core::Proc& p = Core::get_proc(name);
		ProfileInfo* entry = p.profile();

		nlohmann::json resp;
		resp["name"] = name;
		resp["call_count"] = entry->call_count;
		resp["self"] = { {"seconds", entry->self.seconds}, {"microseconds", entry->self.seconds} };
		resp["total"] = { {"seconds", entry->total.seconds}, {"microseconds", entry->total.seconds} };
		resp["real"] = { {"seconds", entry->real.seconds}, {"microseconds", entry->real.seconds} };
		resp["overtime"] = { {"seconds", entry->overtime.seconds}, {"microseconds", entry->overtime.seconds} };

		data["content"] = resp;
		debugger.send(data);
	}
	else if (type == MESSAGE_TOGGLE_PROFILER)
	{
		bool enable = data.at("content");
		if (enable)
		{
			Core::enable_profiling();
		}
		else
		{
			Core::disable_profiling();
		}
		debugger.send(data);
	}
	else if (type == MESSAGE_CONFIGURATION_DONE)
	{
		return RES_CONFIGURATION_DONE;
	}
	return RES_CONTINUE;
}

void DebugServer::debug_loop()
{
	while (true)
	{
		int res = debug_server.handle_one_message();
		if (res == RES_BREAK)
		{
			return;
		}
	}
}

bool DebugServer::loop_until_configured()
{
	while (true)
	{
		int res = debug_server.handle_one_message();
		if (res == RES_CONFIGURATION_DONE)
		{
			break;
		}
		else if (res != RES_CONTINUE)
		{
			return false;
		}
	}
	std::thread(&DebugServer::debug_loop, &debug_server).detach();
	return true;
}

NextAction DebugServer::wait_for_action()
{
	std::unique_lock<std::mutex> lk(notifier_mutex);
	notifier.wait(lk, [this] { return next_action != NextAction::WAIT; });
	NextAction res = next_action;
	next_action = NextAction::WAIT;
	return res;
}

void DebugServer::send_simple(std::string message_type)
{
	debugger.send({ {"type", message_type} });
}

void DebugServer::send(std::string message_type, nlohmann::json content)
{
	debugger.send({ {"type", message_type}, {"content", content } });
}

void DebugServer::set_breakpoint(int proc_id, int offset, bool singleshot)
{
	if (get_breakpoint(proc_id, offset))
	{
		return;
	}
	Core::Proc& proc = Core::get_proc(proc_id);
	std::uint32_t* bytecode = proc.get_bytecode();
	Breakpoint bp = { //Directly writing to bytecode rather than using set_bytecode,
		&proc, //because this will ensure any running procs will also hit this
		bytecode[offset],
		(unsigned short)offset,
		singleshot
	};
	bytecode[offset] = breakpoint_opcode;
	breakpoints[proc_id][offset] = bp;
}

std::optional<Breakpoint> DebugServer::get_breakpoint(int proc_id, int offset)
{
	if (breakpoints.find(proc_id) != breakpoints.end())
	{
		if (breakpoints[proc_id].find(offset) != breakpoints[proc_id].end())
		{
			return breakpoints[proc_id][offset];
		}
	}
	return {};
}

void DebugServer::remove_breakpoint(int proc_id, int offset)
{
	auto bp = get_breakpoint(proc_id, offset);
	if (!bp)
	{
		Core::Alert("Attempted to remove nonexistent breakpoint");
		return;
	}
	if (bp->replaced_opcode == breakpoint_opcode)
	{
		Core::Alert("removed breakpoint has replaced breakpoint_opcode");
		return;
	}
	std::uint32_t* bytecode = Core::get_proc(proc_id).get_bytecode();
	bytecode[bp->offset] = bp->replaced_opcode;
	breakpoints[proc_id].erase(offset);
	breakpoint_to_restore = {};
}

void DebugServer::restore_breakpoint()
{
	if (!breakpoint_to_restore)
	{
		Core::Alert("Restore() called with no breakpoint to restore");
		return;
	}
	std::uint32_t* bytecode = breakpoint_to_restore->proc->get_bytecode();
	std::swap(bytecode[breakpoint_to_restore->offset], breakpoint_to_restore->replaced_opcode);
	breakpoint_to_restore = {};
}

void DebugServer::on_breakpoint(ExecutionContext* ctx)
{
	auto bp = get_breakpoint(ctx->constants->proc_id, ctx->current_opcode);
	std::swap(ctx->bytecode[bp->offset], bp->replaced_opcode);
	if (!bp->one_shot)
	{
		breakpoint_to_restore = bp;
	}
	send_call_stack(ctx);
	send(MESSAGE_BREAKPOINT_HIT, { {"proc", bp->proc->name }, {"offset", bp->offset }, {"override_id", Core::get_proc(ctx).override_id}, {"reason", "breakpoint opcode"} });
	on_break(ctx);
	ctx->current_opcode--;
}

void DebugServer::on_step(ExecutionContext* ctx)
{
	auto& proc = Core::get_proc(ctx);
	send_call_stack(ctx);
	send(MESSAGE_BREAKPOINT_HIT, { {"proc", proc.name }, {"offset", ctx->current_opcode }, {"override_id", proc.override_id}, {"reason", "step"} });
	on_break(ctx);
}

void DebugServer::on_break(ExecutionContext* ctx)
{
	switch (wait_for_action())
	{
	case NextAction::STEP_INTO:
		step_mode = StepMode::INTO;
		break;
	case NextAction::STEP_OVER:
		step_mode = StepMode::PRE_OVER;
		step_over_context = ctx;
		step_over_parent_context = ctx->parent_context;
		break;
	case NextAction::RESUME:
		step_mode = StepMode::NONE;
		break;
	}
}

void DebugServer::on_error(ExecutionContext* ctx, const char* error)
{
	Core::Proc& p = Core::get_proc(ctx);
	send_call_stack(ctx);
	debug_server.send(MESSAGE_RUNTIME, { {"proc", p.name }, {"offset", ctx->current_opcode }, {"override_id", p.override_id}, {"message", std::string(error)} });
	debug_server.wait_for_action();
}

nlohmann::json value_to_text(Value val)
{
	nlohmann::json literal;
	switch (val.type)
	{
	case DataType::NUMBER:
		literal = { { "number", val.valuef } };
		break;
	case DataType::STRING:
		literal = { { "string", GetStringTableEntry(val.value)->stringData } };
		break;
	case DataType::MOB_TYPEPATH:
		literal = { { "typepath", Core::type_to_text(*MobTableIndexToGlobalTableIndex(val.value)) } };
		break;
	case DataType::OBJ_TYPEPATH:
	case DataType::TURF_TYPEPATH:
	case DataType::AREA_TYPEPATH:
	case DataType::DATUM_TYPEPATH:
		literal = { { "typepath", Core::type_to_text(val.value) } };
		break;
	case DataType::LIST_TYPEPATH:
		// Not subtypeable
		literal = { { "typepath", "/list" } };
		break;
	case DataType::CLIENT_TYPEPATH:
		// Not subtypeable
		literal = { { "typepath", "/client" } };
		break;
	case DataType::SAVEFILE_TYPEPATH:
		// Not subtypeable
		literal = { { "typepath", "/savefile" } };
		break;
	case DataType::RESOURCE:
		literal = { { "resource", Core::stringify(val) } };
		break;
	case DataType::PROCPATH: {
		if (val.value < Core::get_all_procs().size()) {
			Core::Proc& p = Core::get_proc(val.value);
			literal = { { "proc", p.name } };
		} else {
			literal = { { "ref", (val.type << 24) | val.value } };
		}
		break;
	}
	default:
		literal = { { "ref", (val.type << 24) | val.value } };
	}
	nlohmann::json result = { { "literal", literal } };

	switch (val.type)
	{
	case DataType::TURF:
	case DataType::OBJ:
	case DataType::MOB:
	case DataType::AREA:
	case DataType::CLIENT:
	case DataType::IMAGE:
	case DataType::WORLD_D:
	case DataType::DATUM:
	case DataType::SAVEFILE:
		result["has_vars"] = true;
	}

	switch (val.type)
	{
	case DataType::LIST:
	/*case LIST_ARGS: //uncomment when handled in GET_LIST_CONTENTS
	case LIST_VERBS:
	case LIST_CONTENTS_2:
	case LIST_CONTENTS:
	case LIST_VARS:*/
		result["is_list"] = true;
	}

	return result;
}

void DebugServer::send_call_stack(ExecutionContext* ctx)
{
	std::vector<nlohmann::json> res;
	do
	{
		nlohmann::json j;
		Core::Proc& p = Core::get_proc(ctx);

		j["proc"] = p.name;
		j["override_id"] = p.override_id;
		j["offset"] = ctx->current_opcode;

		j["usr"] = value_to_text(ctx->constants->usr);
		j["src"] = value_to_text(ctx->constants->src);
		j["dot"] = value_to_text(ctx->dot);

		std::vector<nlohmann::json> locals;
		for (int i = 0; i < ctx->local_var_count; i++)
			locals.push_back(value_to_text(ctx->local_variables[i]));
		j["locals"] = locals;

		std::vector<nlohmann::json> args;
		for (int i = 0; i < ctx->constants->arg_count; i++)
			args.push_back(value_to_text(ctx->constants->args[i]));
		j["args"] = args;

		res.push_back(j);
	} while(ctx = ctx->parent_context);
	debug_server.send(MESSAGE_CALL_STACK, res);
}

void on_nop(ExecutionContext* ctx)
{

}

void hRuntime(const char* error)
{
	if (debug_server.break_on_runtimes)
	{
		debug_server.on_error(Core::get_context(), error);
	}
	oRuntime(error);
}



/*trvh hGetVariable(int datumType, int datumId, unsigned int name_id)
{
	if (debug_server.data_breakpoints_read.find(datumType) != debug_server.data_breakpoints_read.end())
	{
		auto breakpoints_for_type = debug_server.data_breakpoints_read.at(datumType);
		if (breakpoints_for_type.find(datumId) != breakpoints_for_type.end())
		{
			auto breakpoint_var_names = breakpoints_for_type.equal_range(datumId);
			for (auto it = breakpoint_var_names.first; it != breakpoint_var_names.second; ++it)
			{
				if (it->second == name_id)
				{
					//debug_server.on_data_breakpoint(Core::get_context(), datumType, datumId, Core::GetStringFromId(name_id));
				}
			}
		}
	}
	return oGetVariable(datumType, datumId, name_id);
}*/

extern "C" void on_singlestep()
{
	if (debug_server.breakpoint_to_restore && Core::get_context()->current_opcode != debug_server.breakpoint_to_restore->offset)
	{
		debug_server.restore_breakpoint();
	}
	if (debug_server.step_mode == StepMode::INTO)
	{
		debug_server.on_step(Core::get_context());
	}
	else if (debug_server.step_mode == StepMode::OVER)
	{
		if (!debug_server.step_over_context)
		{
			debug_server.step_mode = StepMode::NONE;
			return;
		}
		ExecutionContext* ctx = Core::get_context();
		if (debug_server.step_over_context == ctx || debug_server.step_over_parent_context == ctx)
		{
			debug_server.step_over_context = nullptr;
			debug_server.step_over_parent_context = nullptr;
			debug_server.on_step(Core::get_context());
		}
		if (!ctx->parent_context && (ctx->bytecode[ctx->current_opcode] == RET || ctx->bytecode[ctx->current_opcode] == END))
		{
			debug_server.step_over_context = nullptr; //there is nothing to return to, we missed our chance
			debug_server.step_over_parent_context = nullptr;
			debug_server.step_mode = StepMode::NONE;
		}
	}
	else if (debug_server.step_mode == StepMode::PRE_OVER)
	{
		debug_server.step_mode = StepMode::OVER;
	}
}

void on_breakpoint(ExecutionContext* ctx)
{
	debug_server.on_breakpoint(ctx);
}

#ifdef _MSC_VER
__declspec(naked) void singlestep_hook()
{
	_asm
	{
		// If you modify this, modify singlestep_hook.s as well.
		push eax
		mov edx, on_singlestep
		call edx
		pop eax
		MOVZX ECX, WORD PTR DS : [EAX + 0x14]
		MOV EDI, DWORD PTR DS : [EAX + 0x10]
		ret
	}
}
#else
#ifdef WIN32
extern "C" void singlestep_hook();
#else
void singlestep_hook() {}
#endif
#endif

/*
MOVZX ECX, WORD PTR DS:[EAX + 0x14]
MOV EDI, DWORD PTR DS:[EAX + 0x10]

Below remains after hooking
MOV ESI, ECX
MOV EDX, DWORD PTR DS:[EDI + ESI * 4]
CMP EDX, 0x143
*/

#define nth(x, n) (x >> (n * 8)) & 0xFF;

void install_singlestep_hook()
{
	char* opcode_switch = (char*)Pocket::Sigscan::FindPattern("byondcore.dll", "0F B7 48 14 8B 78 10 8B F1 8B 14 B7 81 FA");
	std::uint32_t addr = (std::uint32_t) & singlestep_hook;
#ifdef _WIN32
	DWORD old_prot;
	VirtualProtect((void*)opcode_switch, 16, PAGE_EXECUTE_READWRITE, &old_prot);
#endif
	opcode_switch[0] = 0xBA; //MOV EDX,
	opcode_switch[1] = nth(addr, 0);
	opcode_switch[2] = nth(addr, 1);
	opcode_switch[3] = nth(addr, 2);
	opcode_switch[4] = nth(addr, 3); //address of singlestep_hook
	opcode_switch[5] = 0xFF; //CALL
	opcode_switch[6] = 0xD2; //EDX
#ifdef _WIN32
	VirtualProtect((void*)opcode_switch, 16, old_prot, &old_prot);
#endif
}

bool debugger_initialize()
{
#ifdef _WIN32
	static bool debugger_initialized = false;
	if (debugger_initialized)
	{
		return true;
	}

	oRuntime = Core::install_hook(Runtime, hRuntime);
	install_singlestep_hook();
	breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", on_breakpoint);
	nop_opcode = Core::register_opcode("DEBUG_NOP", on_nop);
	debugger_initialized = true;
	return true;
#else
	Core::alert_dd("The extools debugger is not supported on Linux.");
	return false;
#endif
}

bool debugger_enable(const char* mode, const char* port)
{
	if (!strcmp(mode, DBG_MODE_LAUNCHED))
	{
		// launched mode
		if (!debug_server.connect(port))
		{
			return false;
		}
		return debug_server.loop_until_configured();
	}
	else if (!strcmp(mode, DBG_MODE_BLOCK))
	{
		if (!debug_server.listen(port))
		{
			return false;
		}
		return debug_server.loop_until_configured();
	}
	else if (!strcmp(mode, DBG_MODE_BACKGROUND))
	{
		std::string portbuf = port;
		std::thread([portbuf]() {
			debug_server.listen(portbuf.c_str());
			debug_server.debug_loop();
		}).detach();
		return true;
	}
	else
	{
		return false;
	}
}
