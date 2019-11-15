#include "debug_server.h"
#include "../dmdism/disassembly.h"
#include "../core/json.hpp"

#include <thread>

int breakpoint_opcode;
int nop_opcode;

std::unordered_map<unsigned short, std::vector<Breakpoint>> breakpoints;

DebugServer debug_server;

bool DebugServer::connect()
{
	debugger = SocketServer();
	return debugger.listen_for_client();
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
			debugger.sendall(MESSAGE_RAW, echoing);
		}
		else if (type == MESSAGE_PROC_LIST)
		{
			std::vector<std::string> procs;
			for (Core::Proc& proc : procs_by_id)
			{
				procs.push_back(proc.name);
			}
			debugger.sendall(MESSAGE_PROC_LIST, procs);
		}
	}

}

Breakpoint get_breakpoint(int proc_id, int offset)
{
	for (Breakpoint& bp : breakpoints[proc_id])
	{
		if (bp.offset == offset)
		{
			return bp;
		}
	}
}

void on_breakpoint(ExecutionContext* ctx)
{
	Core::Alert("Hi!");
	Breakpoint bp = get_breakpoint(ctx->constants->proc_id, ctx->current_opcode);
}

void on_nop(ExecutionContext* ctx)
{

}

Breakpoint set_breakpoint(Core::Proc proc, int instruction_index, bool one_shot = false)
{
	Breakpoint bp;
	bp.replaced_instruction = Instruction::create(breakpoint_opcode);
	bp.proc = proc;
	bp.instruction_index = instruction_index;
	bp.one_shot = false;
	Disassembly disassembly = proc.disassemble();
	std::swap(disassembly.at(instruction_index), bp.replaced_instruction);
	proc.assemble(disassembly);
	breakpoints[proc.id].push_back(bp);
	return bp;
}

bool remove_breapoint(Breakpoint bp)
{
	Disassembly disassembly = bp.proc.disassemble();
	std::swap(disassembly.at(bp.instruction_index), bp.replaced_instruction);
	bp.proc.assemble(disassembly);
	return true;
}

bool debugger_initialize()
{
	breakpoint_opcode = Core::register_opcode("DEBUG_BREAKPOINT", on_breakpoint);
	nop_opcode = Core::register_opcode("DEBUG_NOP", on_nop);
	return true;
}

bool debugger_connect()
{
	debug_server = DebugServer();
	if (debug_server.connect())
	{
		std::thread(&DebugServer::debug_loop, debug_server).detach();
		return true;
	}
	return false;
}