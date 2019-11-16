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
			debugger.sendall(MESSAGE_PROC_DISASSEMBLY, disassembled_proc);
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

Breakpoint set_breakpoint(Core::Proc proc, int offset, bool one_shot = false)
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