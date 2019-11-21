#include "debug_server.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../dmdism/opcodes.h"
#include "../core/json.hpp"

#include <thread>

int breakpoint_opcode;
int nop_opcode;
int singlestep_opcode;

std::unordered_map<unsigned short, std::vector<Breakpoint>> breakpoints;
std::unordered_map<unsigned short, std::vector<BreakpointRestorer>> singlesteps;
std::unique_ptr<Breakpoint> breakpoint_to_restore;

DebugServer debug_server;
std::mutex notifier_mutex;
std::condition_variable notifier;

bool DebugServer::connect()
{
	debugger = SocketServer();
	return debugger.listen_for_client();
}

Breakpoint set_breakpoint(Core::Proc proc, int offset, bool one_shot = false);

void stripUnicode(std::string& str)
{
	str.erase(remove_if(str.begin(), str.end(), [](unsigned char c) {return !(c >= 0 && c < 128); }), str.end());
}

void DebugServer::debug_loop()
{
	while (true)
	{
		nlohmann::json data = debugger.recv_message();
		if (data.is_null())
		{
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
			std::vector<std::string> procs;
			for (Core::Proc& proc : procs_by_id)
			{
				procs.push_back(proc.name);
			}
			debugger.send(MESSAGE_PROC_LIST, procs);
		}
		else if (type == MESSAGE_PROC_DISASSEMBLY)
		{
			const std::string& proc_name = data.at("content");
			Core::Proc proc = Core::get_proc(proc_name);
			Disassembly disassembly = proc.disassemble();
			nlohmann::json disassembled_proc;
			disassembled_proc["name"] = proc_name;

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
					{ "possible_jumps", instr.jump_locations() }
				};
				instructions.push_back(d_instr);
			}
			disassembled_proc["instructions"] = instructions;
			debugger.send(MESSAGE_PROC_DISASSEMBLY, disassembled_proc);
		}
		else if (type == MESSAGE_BREAKPOINT_SET)
		{
			auto content = data.at("content");
			const std::string& proc = content.at("proc");
			set_breakpoint(proc, content.at("offset"), false);
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

std::vector<std::string> get_call_stack(ExecutionContext* ctx)
{
	std::vector<std::string> res;
	do
	{
		res.push_back(Core::get_proc(ctx->constants->proc_id));
		ctx = ctx->parent_context;
	} while(ctx);
	return res;
}

void update_readouts(ExecutionContext* ctx)
{
	send_values(MESSAGE_VALUES_LOCALS, ctx->local_variables, ctx->local_var_count);
	send_values(MESSAGE_VALUES_ARGS, ctx->constants->args, ctx->constants->arg_count);
	send_values(MESSAGE_VALUES_STACK, ctx->stack, ctx->stack_size);
	debug_server.send(MESSAGE_CALL_STACK, get_call_stack(ctx));
}

bool place_restorer_on_next_instruction(ExecutionContext* ctx, unsigned int offset)
{
	Core::Proc p = Core::get_proc(ctx->constants->proc_id);
	Disassembly current_dis = Disassembler(reinterpret_cast<std::uint32_t*>(ctx->bytecode), p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		BreakpointRestorer sbp = {
			(int)next->bytes().at(0), (int)offset, (int)next->offset()
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
	Disassembly current_dis = Disassembler(reinterpret_cast<std::uint32_t*>(ctx->bytecode), p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		Breakpoint bp = {
			p, (int)next->bytes().at(0), (int)next->offset(), true
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
	debug_server.send(MESSAGE_BREAKPOINT_HIT, { {"proc", bp->proc.name }, {"offset", bp->offset } });
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
	ctx->bytecode[sbp->offset_to_replace] = sbp->replaced_opcode;
	auto ss = singlesteps[ctx->constants->proc_id];
	ss.erase(std::remove(ss.begin(), ss.end(), *sbp), ss.end());
	ctx->current_opcode--;
}

Breakpoint set_breakpoint(Core::Proc proc, int offset, bool one_shot)
{
	Breakpoint bp = {
		proc, breakpoint_opcode, offset, one_shot
	};
	int* bytecode = proc.get_bytecode();
	std::swap(bytecode[offset], bp.replaced_opcode);
	proc.set_bytecode(bytecode);
	breakpoints[proc.id].push_back(bp);
	return bp;
}

bool remove_breakpoint(Breakpoint bp)
{
	int* bytecode = bp.proc.get_bytecode();
	std::swap(bytecode[bp.offset], bp.replaced_opcode);
	bp.proc.set_bytecode(bytecode);
	auto bps = breakpoints[bp.proc.id];
	bps.erase(std::remove(bps.begin(), bps.end(), bp), bps.end());
	return true;
}

bool debugger_initialize()
{
	breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", on_breakpoint);
	nop_opcode = Core::register_opcode("DEBUG_NOP", on_nop);
	singlestep_opcode = Core::register_opcode("DEBUG_SINGLESTEP", on_restorer);
	return true;
}

bool debugger_connect()
{
	debug_server = DebugServer();
	if (debug_server.connect())
	{
		std::thread(&DebugServer::debug_loop, &debug_server).detach();
		return true;
	}
	return false;
}