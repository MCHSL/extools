#include "debug_server.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../dmdism/opcodes.h"
#include "../core/json.hpp"
#include <utility>

#include <thread>

std::uint32_t breakpoint_opcode;
std::uint32_t nop_opcode;
std::uint32_t singlestep_opcode;

std::unordered_map<unsigned short, std::vector<Breakpoint>> breakpoints;
std::unordered_map<unsigned short, std::vector<BreakpointRestorer>> singlesteps;
std::unique_ptr<Breakpoint> breakpoint_to_restore;

DebugServer debug_server;
std::mutex notifier_mutex;
std::condition_variable notifier;

RuntimePtr oRuntime;

bool DebugServer::connect()
{
	debugger = SocketServer();
	return debugger.listen_for_client();
}

Breakpoint set_breakpoint(Core::Proc proc, std::uint16_t offset, bool one_shot = false);
std::unique_ptr<Breakpoint> get_breakpoint(Core::Proc proc, int offset);
bool remove_breakpoint(Breakpoint bp);

void stripUnicode(std::string& str)
{
	str.erase(remove_if(str.begin(), str.end(), [](unsigned char c) {return !(c >= 0 && c < 128); }), str.end());
}

nlohmann::json value_to_text(Value val);

int datatype_name_to_val(std::string name)
{
	for (auto it = datatype_names.begin(); it != datatype_names.end(); ++it)
		if (it->second == name)
			return it->first;
	return DataType::NULL_D;
}

void get_variable_error(int field_name)
{
	Core::Alert("An error occured while trying to access field \"" + Core::GetStringFromId(field_name) + "\" of datum. ");
}

void DebugServer::debug_loop()
{
	while (true)
	{
		nlohmann::json data = debugger.recv_message();
		//Core::Alert("Message!!");
		if (data.is_null())
		{
			Core::Alert("null message, leaving");
			break;
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
			for (Core::Proc& proc : procs_by_id)
			{
				procs.push_back({ {"name", proc.name}, {"override_id", proc.override_id} });
			}
			debugger.send(MESSAGE_PROC_LIST, procs);
		}
		else if (type == MESSAGE_PROC_DISASSEMBLY)
		{
			auto content = data.at("content");
			const std::string& proc_name = content.at("name");
			//Core::Alert("Disassembling " + proc_name);
			const int& override_id = content.at("override_id");
			Core::Proc proc = Core::get_proc(proc_name, override_id);
			Disassembly disassembly = proc.disassemble();
			nlohmann::json disassembled_proc;
			disassembled_proc["name"] = proc_name;
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
			set_breakpoint(Core::get_proc(proc, override_id), content.at("offset"));
			debugger.send(data);
		}
		else if (type == MESSAGE_BREAKPOINT_UNSET)
		{
			auto content = data.at("content");
			const std::string& proc = content.at("proc");
			const int& override_id = content.at("override_id");
			//Core::Alert("Setting breakpoint in " + proc);
			remove_breakpoint(Core::get_proc(proc, override_id), content.at("offset"));
			debugger.send(data);
		}
		else if (type == MESSAGE_BREAKPOINT_STEP_INTO)
		{
			std::lock_guard<std::mutex> lk(notifier_mutex);
			next_action = STEP_INTO;
			notifier.notify_all();
		}
		else if (type == MESSAGE_BREAKPOINT_RESUME)
		{
			next_action = RESUME;
			notifier.notify_all();
		}
		else if (type == MESSAGE_GET_FIELD)
		{
			auto content = data.at("content");
			data["content"] = value_to_text(Value(datatype_name_to_val(content.at("datum_type")), content.at("datum_id")).get_safe(content.at("field_name")));
			debugger.send(data);
		}
		else if (type == MESSAGE_GET_ALL_FIELDS)
		{
			auto content = data.at("content");
			Value datum = Value(datatype_name_to_val(content.at("datum_type")), content.at("datum_id"));
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
		else if (type == MESSAGE_GET_TYPE)
		{
			auto content = data.at("content");
			Value typeval = GetVariable(datatype_name_to_val(content.at("datum_type")), content.at("datum_id"), Core::GetStringId("type"));
			if (typeval.type == DataType::MOB_TYPEPATH)
			{
				typeval.value = *MobTableIndexToGlobalTableIndex(typeval.value);
			}
			data["content"] = Core::type_to_text(typeval.value);
			debugger.send(data);
		}
		else if (type == MESSAGE_TOGGLE_BREAK_ON_RUNTIME)
		{
			break_on_runtimes = !break_on_runtimes; //runtimes funtimes
			data["content"] = break_on_runtimes;
			debugger.send(data);
		}
		else if (type == MESSAGE_GET_LIST_CONTENTS)
		{
			List list(data.at("content"));
			std::vector<Value> elements = std::vector<Value>(list.list->vector_part, list.list->vector_part + list.list->length); //efficiency
			std::vector<nlohmann::json> textual;
			if (!list.is_assoc())
			{
				for (Value& val : elements)
				{
					textual.push_back(value_to_text(val));
				}
			}
			else
			{
				for (Value& val : elements)
				{
					textual.push_back(std::make_pair<nlohmann::json, nlohmann::json>(value_to_text(val), value_to_text(list.at(val))));
				}
			}
			data["content"] = {
				{ "is_assoc", list.is_assoc() },
				{ "elements", textual }
			};
			debugger.send(data);
		}
		else if (type == MESSAGE_GET_PROFILE)
		{
			const std::string& name = data.at("content");
			Core::Proc p = Core::get_proc(name);
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
		else if (type == MESSAGE_ENABLE_PROFILER)
		{
			Core::enable_profiling();
			debugger.send(data);
		}
		else if (type == MESSAGE_DISABLE_PROFILER)
		{
			Core::disable_profiling();
			debugger.send(data);
		}
	}
}

NextAction DebugServer::wait_for_action()
{
	std::unique_lock<std::mutex> lk(notifier_mutex);
	notifier.wait(lk, [this] { return next_action != WAIT; });
	NextAction res = next_action;
	next_action = WAIT;
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
	std::uint32_t* bytecode = Core::get_proc(proc_id).get_bytecode(); 
	Breakpoint bp = { //Directly writing to bytecode rather than using set_bytecode, 
		Core::get_proc(proc_id), //because this will ensure any running procs will also hit this
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
	}
	std::uint32_t* bytecode = Core::get_proc(proc_id).get_bytecode();
	bytecode[bp->offset] = bp->replaced_opcode;
	breakpoints[proc_id].erase(offset);
}

void DebugServer::restore_breakpoint()
{
	if (!breakpoint_to_restore)
	{
		Core::Alert("Restore() called with no breakpoint to restore");
		return;
	}
	std::uint32_t* bytecode = breakpoint_to_restore->proc.get_bytecode();
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
	send(MESSAGE_BREAKPOINT_HIT, { {"proc", bp->proc.name }, {"offset", bp->offset }, {"override_id", Core::get_proc(ctx).override_id} });
	on_break(ctx);
	has_stepped_after_replacing_breakpoint_opcode = false;
	ctx->current_opcode--;
}

void DebugServer::on_step(ExecutionContext* ctx)
{
	auto proc = Core::get_proc(ctx);
	send(MESSAGE_BREAKPOINT_HIT, { {"proc", proc.name }, {"offset", ctx->current_opcode }, {"override_id", proc.override_id} });
	on_break(ctx);
}

void DebugServer::on_break(ExecutionContext* ctx)
{
	send_call_stack(ctx);
	switch (wait_for_action())
	{
	case STEP_INTO:
		break_on_step = true;
		break;
	case RESUME:
		break_on_step = false;
		break;
	}
}

void DebugServer::on_error(ExecutionContext* ctx, char* error)
{
	Core::Proc p = Core::get_proc(ctx);
	debug_server.send(MESSAGE_RUNTIME, { {"proc", p.name }, {"offset", ctx->current_opcode }, {"override_id", p.override_id}, {"message", std::string(error)} });
	send_call_stack(ctx);
	while (debug_server.wait_for_action() != RESUME);
}

nlohmann::json value_to_text(Value val)
{
	std::string type_text = "UNKNOWN TYPE (" + std::to_string(val.type) + ")";
	if (datatype_names.find((DataType)val.type) != datatype_names.end())
	{
		type_text = datatype_names.at((DataType)val.type);
	}
	std::string value_text;
	switch (val.type)
	{
	case NUMBER:
		value_text = std::to_string(val.valuef);
		break;
	case STRING:
		value_text = GetStringTableEntry(val.value)->stringData;
		break;
	default:
		value_text = std::to_string(val.value);
	}
	return { { "type", type_text }, { "value", value_text } };
}

void DebugServer::send_call_stack(ExecutionContext* ctx)
{
	std::vector<nlohmann::json> res;
	do
	{
		nlohmann::json j;
		Core::Proc p = Core::get_proc(ctx);
		j["name"] = p.name;
		j["override_id"] = p.override_id;
		j["usr"] = value_to_text(ctx->constants->usr);
		j["src"] = value_to_text(ctx->constants->src);

		std::vector<nlohmann::json> locals;
		for (int i = 0; i < ctx->local_var_count; i++)
			locals.push_back(value_to_text(ctx->local_variables[i]));
		j["locals"] = locals;

		std::vector<nlohmann::json> args;
		for (int i = 0; i < ctx->constants->arg_count; i++)
			args.push_back(value_to_text(ctx->constants->args[i]));
		j["args"] = args;

		j["instruction_pointer"] = ctx->current_opcode;
		res.push_back(j);
	} while(ctx = ctx->parent_context);
	debug_server.send(MESSAGE_CALL_STACK, res);
}

void on_nop(ExecutionContext* ctx)
{

}

void hRuntime(char* error)
{
	if (debug_server.break_on_runtimes)
	{
		debug_server.on_error(Core::get_context(), error);
	}
	oRuntime(error);
}

void on_singlestep()
{
	if (debug_server.breakpoint_to_restore && Core::get_context()->current_opcode != debug_server.breakpoint_to_restore->offset)
	{
		debug_server.restore_breakpoint();
	}
	if (debug_server.break_on_step)
	{
		if (!debug_server.has_stepped_after_replacing_breakpoint_opcode)
		{
			debug_server.has_stepped_after_replacing_breakpoint_opcode = true;
		}
		else
		{
			debug_server.on_step(Core::get_context());
		}
	}
}

void on_breakpoint(ExecutionContext* ctx)
{
	debug_server.on_breakpoint(ctx);
}

__declspec(naked) void singlestep_hook()
{
	_asm
	{
		push eax
		mov edx, on_singlestep
		call edx
		pop eax
		MOVZX ECX, WORD PTR DS : [EAX + 0x14]
		MOV EDI, DWORD PTR DS : [EAX + 0x10]
		ret
	}
}

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
	DWORD old_prot;
	VirtualProtect((void*)opcode_switch, 16, PAGE_EXECUTE_READWRITE, &old_prot);
	opcode_switch[0] = 0xBA; //MOV EDX,
	opcode_switch[1] = nth(addr, 0);
	opcode_switch[2] = nth(addr, 1);
	opcode_switch[3] = nth(addr, 2);
	opcode_switch[4] = nth(addr, 3); //address of singlestep_hook
	opcode_switch[5] = 0xFF; //CALL
	opcode_switch[6] = 0xD2; //EDX

}

bool debugger_initialize()
{
	oRuntime = (RuntimePtr)Core::install_hook((void*)Runtime, (void*)hRuntime);
	install_singlestep_hook();
	breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", on_breakpoint);
	nop_opcode = Core::register_opcode("DEBUG_NOP", on_nop);
	//singlestep_opcode = Core::register_opcode("DEBUG_SINGLESTEP", on_restorer);
	return true;
}

bool debugger_enable_wait(bool pause)
{
	debug_server = DebugServer();
	if (debug_server.connect())
	{
		std::thread(&DebugServer::debug_loop, &debug_server).detach();
		if (pause)
		{
			debug_server.break_on_step = true;
		}
		return true;
	}
	return false;
}

void debugger_enable()
{
	std::thread(debugger_enable_wait, false).detach(); //I am good code
}