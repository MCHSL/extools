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

/*trvh safe_get_variable(int datum_type, int datum_id, int field_name)
{
	__try
	{
		return GetVariable(datum_type, datum_id, field_name);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		//get_variable_error(field_name);
		return {};
	}
}*/

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
			set_breakpoint(Core::get_proc(proc, override_id), content.at("offset"), false);
			debugger.send(data);
		}
		else if (type == MESSAGE_BREAKPOINT_UNSET)
		{
			auto content = data.at("content");
			const std::string& proc = content.at("proc");
			const int& override_id = content.at("override_id");
			//Core::Alert("Setting breakpoint in " + proc);
			remove_breakpoint(*get_breakpoint(Core::get_proc(proc, override_id), content.at("offset")));
			debugger.send(data);
		}
		else if (type == MESSAGE_BREAKPOINT_STEP)
		{
			std::lock_guard<std::mutex> lk(notifier_mutex);
			next_action = DEBUG_STEP;
			notifier.notify_all();
		}
		else if (type == MESSAGE_BREAKPOINT_RESUME)
		{
			next_action = DEBUG_RESUME;
			notifier.notify_all();
		}
		else if (type == MESSAGE_GET_FIELD)
		{
			auto content = data.at("content");
			data["content"] = value_to_text(Value(datatype_name_to_val(content.at("datum_type")), content.at("datum_id")).get_safe(content.at("field_name")));
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

int DebugServer::wait_for_action()
{
	std::unique_lock<std::mutex> lk(notifier_mutex);
	notifier.wait(lk, [this] { return next_action != DEBUG_WAIT; });
	int res = next_action;
	next_action = DEBUG_WAIT;
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

void send_values(std::string message_type, Value* values, unsigned int count)
{
	std::vector<nlohmann::json> c;
	for (int i = 0; i < count; i++)
	{
		c.push_back(value_to_text(values[i]));
	}
	debug_server.send(message_type, c);
}

void send_call_stack(ExecutionContext* ctx)
{
	std::vector<nlohmann::json> res;
	do
	{
		nlohmann::json j;
		Core::Proc p = Core::get_proc(ctx->constants->proc_id);
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

void update_readouts(ExecutionContext* ctx)
{
	send_values(MESSAGE_VALUES_LOCALS, ctx->local_variables, ctx->local_var_count);
	send_values(MESSAGE_VALUES_ARGS, ctx->constants->args, ctx->constants->arg_count);
	send_values(MESSAGE_VALUES_STACK, ctx->stack, ctx->stack_size);
	send_call_stack(ctx);
}

bool place_restorer_on_next_instruction(ExecutionContext* ctx, std::uint16_t offset)
{
	Core::Proc p = Core::get_proc(ctx->constants->proc_id);
	Disassembly current_dis = Disassembler(ctx->bytecode, p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		BreakpointRestorer sbp = {
			current_dis.at(offset).bytes().at(0), next->bytes().at(0), offset, next->offset()
		};
		ctx->bytecode[next->offset()] = singlestep_opcode;
		singlesteps[p.id].push_back(sbp);
		return true;
	}
	return false;
}

bool place_breakpoint_on_next_instruction(ExecutionContext* ctx, unsigned int offset) //TODO: make this and the above better
{
	Core::Proc p = Core::get_proc(ctx->constants->proc_id);
	Disassembly current_dis = Disassembler(ctx->bytecode, p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		Breakpoint bp = {
			p, next->bytes().at(0), next->offset(), true
		};
		ctx->bytecode[next->offset()] = breakpoint_opcode;
		breakpoints[p.id].push_back(bp);
		return true;
	}
	return false;
}

std::unique_ptr<Breakpoint> get_breakpoint(Core::Proc proc, int offset)
{
	for (Breakpoint& bp : breakpoints[proc.id])
	{
		if (bp.offset == offset)
		{
			return std::make_unique<Breakpoint>(bp);
		}
	}
	return nullptr;
}

std::unique_ptr<BreakpointRestorer> get_restorer(Core::Proc proc, int offset)
{
	for (BreakpointRestorer& bp : singlesteps[proc.id])
	{
		if (bp.my_offset == offset)
		{
			return std::make_unique<BreakpointRestorer>(bp);
		}
	}
	return nullptr;
}

void on_breakpoint(ExecutionContext* ctx)
{
	if (breakpoint_to_restore)
	{
		std::swap(ctx->bytecode[breakpoint_to_restore->offset], breakpoint_to_restore->replaced_opcode);
		breakpoint_to_restore = nullptr;
	}
	auto bp = get_breakpoint(ctx->constants->proc_id, ctx->current_opcode);
	std::swap(ctx->bytecode[bp->offset], bp->replaced_opcode);
	debug_server.send(MESSAGE_BREAKPOINT_HIT, { {"proc", bp->proc.name }, {"offset", bp->offset }, {"override_id", Core::get_proc(ctx->constants->proc_id).override_id} });
	update_readouts(ctx);
	switch (debug_server.wait_for_action())
	{
	case DEBUG_STEP:
		if (!bp->one_shot)
		{
			breakpoint_to_restore = std::move(bp);
		}
		place_breakpoint_on_next_instruction(ctx, ctx->current_opcode);
		break;
	case DEBUG_RESUME:
		if (!bp->one_shot)
		{
			place_restorer_on_next_instruction(ctx, bp->offset);
		}
		break;
	}
	ctx->current_opcode--;
}

void on_nop(ExecutionContext* ctx)
{

}

void on_restorer(ExecutionContext* ctx)
{
	auto sbp = get_restorer(ctx->constants->proc_id, ctx->current_opcode);
	if (!sbp)
	{
		Core::Alert("Restore opcode with no associated restorer");
		return;
	}
	//Core::Alert("SBP replacement opcode: " + std::to_string(sbp->replaced_opcode) + ", at offset: " + std::to_string(sbp->offset_to_replace));
	ctx->bytecode[sbp->offset_to_replace] = sbp->breakpoint_replaced_opcode;
	ctx->bytecode[sbp->my_offset] = sbp->my_replaced_opcode;
	auto& ss = singlesteps[ctx->constants->proc_id];
	ss.erase(std::remove(ss.begin(), ss.end(), *sbp), ss.end());
	ctx->current_opcode--;
}

Breakpoint set_breakpoint(Core::Proc proc, std::uint16_t offset, bool one_shot)
{
	Breakpoint bp = {
		proc, breakpoint_opcode, offset, one_shot
	};
	std::uint32_t* bytecode = proc.get_bytecode();
	std::swap(bytecode[offset], bp.replaced_opcode);
	proc.set_bytecode(bytecode);
	breakpoints[proc.id].push_back(bp);
	return bp;
}

bool remove_breakpoint(Breakpoint bp)
{
	std::uint32_t* bytecode = bp.proc.get_bytecode();
	std::swap(bytecode[bp.offset], bp.replaced_opcode);
	bp.proc.set_bytecode(bytecode);
	auto& bps = breakpoints[bp.proc.id];
	bps.erase(std::remove(bps.begin(), bps.end(), bp), bps.end());
	return true;
}

void hRuntime(char* error)
{
	if (debug_server.break_on_runtimes)
	{
		ExecutionContext* ctx = Core::get_context();
		Core::Proc p = Core::get_proc(ctx->constants->proc_id);
		debug_server.send(MESSAGE_RUNTIME, { {"proc", p.name }, {"offset", ctx->current_opcode }, {"override_id", p.override_id}, {"message", std::string(error)} });
		update_readouts(ctx);
		while (debug_server.wait_for_action() != DEBUG_RESUME);
	}

	oRuntime(error);
}

bool debugger_initialize()
{
	oRuntime = (RuntimePtr)Core::install_hook((void*)Runtime, (void*)hRuntime);
	breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", on_breakpoint);
	nop_opcode = Core::register_opcode("DEBUG_NOP", on_nop);
	singlestep_opcode = Core::register_opcode("DEBUG_SINGLESTEP", on_restorer);
	return true;
}

bool debugger_enable_wait()
{
	debug_server = DebugServer();
	if (debug_server.connect())
	{
		std::thread(&DebugServer::debug_loop, &debug_server).detach();
		return true;
	}
	return false;
}

void debugger_enable()
{
	std::thread(debugger_enable_wait).detach(); //I am good code
}