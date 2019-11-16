#include "debug_server.h"
#include "../dmdism/disassembly.h"
#include "../dmdism/disassembler.h"
#include "../core/json.hpp"

#include <thread>

int breakpoint_opcode;
int nop_opcode;
int singlestep_opcode;

std::unordered_map<unsigned short, std::vector<Breakpoint>> breakpoints;
std::unordered_map<unsigned short, std::vector<BreakpointRestorer>> singlesteps;
std::vector<Breakpoint> breakpoints_to_restore;

DebugServer debug_server;
std::mutex notifier_mutex;
std::condition_variable notifier;

bool DebugServer::connect()
{
	debugger = SocketServer();
	return debugger.listen_for_client();
}

Breakpoint set_breakpoint(Core::Proc proc, int offset, bool one_shot = false);

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
				nlohmann::json d_instr = {
					{ "offset", instr.offset() },
					{ "bytes", instr.bytes_str() },
					{ "mnemonic", instr.opcode().mnemonic() },
					{ "comment", instr.comment() }
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

bool place_restorer_on_next_instruction(ExecutionContext* ctx, unsigned int offset)
{
	Core::Proc p = Core::get_proc(ctx->constants->proc_id);
	Disassembly current_dis = Disassembler(reinterpret_cast<std::uint32_t*>(ctx->bytecode), p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		BreakpointRestorer sbp = {
			(unsigned int)next->bytes().at(0), offset, next->offset()
		};
		ctx->bytecode[next->offset()] = singlestep_opcode;
		singlesteps[p.id].push_back(sbp);
		return true;
	}
}

bool place_breakpoint_on_next_instruction(ExecutionContext* ctx, unsigned int offset) //TODO: make this and the above better
{
	Core::Proc p = Core::get_proc(ctx->constants->proc_id);
	Disassembly current_dis = Disassembler(reinterpret_cast<std::uint32_t*>(ctx->bytecode), p.get_bytecode_length(), procs_by_id).disassemble();
	Instruction* next = current_dis.next_from_offset(offset);
	if (next)
	{
		Breakpoint bp = {
			p, (unsigned int)next->bytes().at(0), next->offset(), true
		};
		ctx->bytecode[next->offset()] = breakpoint_opcode;
		breakpoints[p.id].push_back(bp);
		return true;
	}
}

Breakpoint& get_breakpoint(Core::Proc proc, int offset)
{
	for (Breakpoint& bp : breakpoints[proc.id])
	{
		if (bp.offset == offset)
		{
			return bp;
		}
	}
}

BreakpointRestorer& get_singlestep(Core::Proc proc, int offset)
{
	for (BreakpointRestorer& bp : singlesteps[proc.id])
	{
		if (bp.my_offset == offset)
		{
			return bp;
		}
	}
}

void on_breakpoint(ExecutionContext* ctx)
{
	Breakpoint bp = get_breakpoint(ctx->constants->proc_id, ctx->current_opcode);
	std::swap(ctx->bytecode[bp.offset], bp.replaced_opcode);
	debug_server.send_simple(MESSAGE_BREAKPOINT_HIT);
	switch (debug_server.wait_for_action())
	{
	case DEBUG_STEP:
		place_breakpoint_on_next_instruction(ctx, ctx->current_opcode);
		break;
	case DEBUG_RESUME:
		if (!bp.one_shot)
		{
			place_restorer_on_next_instruction(ctx, bp.offset);
		}
		break;
	}
	ctx->current_opcode--;
}

void on_nop(ExecutionContext* ctx)
{

}

void on_singlestep(ExecutionContext* ctx)
{
	BreakpointRestorer sbp = get_singlestep(ctx->constants->proc_id, ctx->current_opcode);
	ctx->bytecode[sbp.offset_to_replace] = sbp.replaced_opcode;
	auto ss = singlesteps[ctx->constants->proc_id];
	ss.erase(std::remove(ss.begin(), ss.end(), sbp), ss.end());
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
	singlestep_opcode = Core::register_opcode("DEBUG_SINGLESTEP", on_singlestep);
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