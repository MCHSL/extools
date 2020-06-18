#include "Test.h"
#include "DMCompiler.h"

#include "../../core/core.h"
#include "../../dmdism/instruction.h"
#include "../../dmdism/disassembly.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

using namespace asmjit;
using namespace dmjit;

static std::ofstream jit_out("jit_out.txt");
static std::ofstream byteout_out("bytecode.txt");
static std::ofstream blocks_out("blocks.txt");
static asmjit::JitRuntime rt;

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	asmjit::Error err;
};

struct ProcBlock
{
	ProcBlock(unsigned int o) : offset(o) {}
	ProcBlock() : offset(0) {}
	std::vector<Instruction> contents;
	unsigned int offset;
	asmjit::Label label;
};

static std::map<unsigned int, ProcBlock> split_into_blocks(Disassembly& dis, DMCompiler& dmc, size_t& locals_max_count)
{
	std::map<unsigned int, ProcBlock> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = ProcBlock(0);
	std::set<unsigned int> jump_targets;
	for (Instruction& i : dis)
	{
		if((i == Bytecode::SETVAR || i == Bytecode::GETVAR) && i.bytes()[1] == AccessModifier::LOCAL)
		{
			uint32_t local_idx = i.bytes()[2];
			locals_max_count = std::max(locals_max_count, local_idx + 1);
		}

		if (jump_targets.find(i.offset()) != jump_targets.end())
		{
			current_block_offset = i.offset();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
			jump_targets.erase(i.offset());
		}
		blocks[current_block_offset].contents.push_back(i);
		if (i == Bytecode::JZ || i == Bytecode::JMP || i == Bytecode::JMP2 || i == Bytecode::JNZ || i == Bytecode::JNZ2)
		{
			const unsigned int target = i.jump_locations().at(0);
			if (target > i.offset())
			{
				jump_targets.insert(i.jump_locations().at(0));
			}
			else //we need to split a block in twain
			{
				ProcBlock& victim = (--blocks.lower_bound(target))->second; //get the block that contains the target offset
				auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
				ProcBlock new_block = ProcBlock(target);
				new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
				victim.contents.erase(split_point, victim.contents.end()); //split
				blocks[target] = new_block;
			}
			current_block_offset = i.offset()+i.size();
			blocks[current_block_offset] = ProcBlock(current_block_offset);
		}
	}
	
	blocks_out << "BEGIN: " << dis.proc->name << '\n';
	for (auto& [offset, block] : blocks)
	{
		block.label = dmc.newLabel();
		blocks_out << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			blocks_out << i.opcode().tostring() << "\n";
		}
		blocks_out << "\n";
	}
	blocks_out << '\n';

	return blocks;
}

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

static void Emit_PushInteger(DMCompiler& dmc, float not_an_integer)
{
	dmc.pushStack(Variable{Imm(DataType::NUMBER), Imm(EncodeFloat(not_an_integer))});
}

static void Emit_SetLocal(DMCompiler& dmc, int index)
{
	dmc.setLocal(index, dmc.popStack());
}

static void Emit_GetLocal(DMCompiler& dmc, int index)
{
	dmc.pushStack(dmc.getLocal(index));
}

static void Emit_Pop(DMCompiler& dmc)
{
	dmc.popStack();
}

static void Emit_PushValue(DMCompiler& dmc, DataType type, unsigned int value, unsigned int value2 = 0)
{
	if (type == DataType::NUMBER)
	{
		value = value << 16 | value2;
	}

	dmc.pushStack(Variable{Imm(type), Imm(value)});
}

static void Emit_Return(DMCompiler& dmc)
{
	dmc.doReturn();
}

static void Emit_MathOp(DMCompiler& dmc, Bytecode op_type)
{
	auto lhs = dmc.popStack();
	auto rhs = dmc.popStack();

	auto xmm0 = dmc.newXmm();
	auto xmm1 = dmc.newXmm();

	if (lhs.Value.isImm())
	{
		auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(lhs.Value.as<Imm>().value()));
		dmc.movd(xmm0, data);
	}
	else
	{
		dmc.movd(xmm0, lhs.Value.as<x86::Gp>());
	}

	if (rhs.Value.isImm())
	{
		auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(rhs.Value.as<Imm>().value()));
		dmc.movd(xmm1, data);
	}
	else
	{
		dmc.movd(xmm1, rhs.Value.as<x86::Gp>());
	}

	switch (op_type)
	{
		case Bytecode::ADD:
			dmc.addss(xmm0, xmm1);
			break;
		case Bytecode::SUB:
			dmc.subss(xmm0, xmm1);
			break;
		case Bytecode::MUL:
			dmc.mulss(xmm0, xmm1);
			break;
		case Bytecode::DIV:
			dmc.divss(xmm0, xmm1);
			break;
	}

	auto ret = dmc.newUInt32();
	dmc.movd(ret, xmm0);

	dmc.pushStack(Variable{Imm(DataType::NUMBER), ret});
}

static void Emit_CallGlobal(DMCompiler& dmc, uint32_t arg_count, uint32_t proc_id)
{
	//std::vector<Variable> args;

	x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
	args.setSize(sizeof(uint32_t));
	uint32_t arg_i = arg_count;
	while (arg_i--)
	{
		Variable var = dmc.popStack();

		args.setOffset(arg_i * sizeof(Value) + offsetof(Value, type));
		if (var.Type.isImm())
		{
			auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Type.as<Imm>().value()));
			dmc.mov(args, var.Type.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Type.as<x86::Gp>());
		}

		args.setOffset(arg_i * sizeof(Value) + offsetof(Value, value));
		if (var.Value.isImm())
		{
			auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Value.as<Imm>().value()));
			dmc.mov(args, var.Value.as<Imm>());
		}
		else
		{
			dmc.mov(args, var.Value.as<x86::Gp>());
		}		
	}
	args.setOffset(0);

	// dmc.commitLocals();
	// dmc.commitStack();

	x86::Gp args_ptr = dmc.newUIntPtr();
	dmc.lea(args_ptr, args);

	x86::Gp ret_type = dmc.newUIntPtr();
	x86::Gp ret_value = dmc.newUIntPtr();

	auto call = dmc.call((uint64_t)CallProcByName, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int*, int, int, int>());
	call->setArg(0, Imm(0));
	call->setArg(1, Imm(0));
	call->setArg(2, Imm(2));
	call->setArg(3, Imm(proc_id));
	call->setArg(4, Imm(0));
	call->setArg(5, Imm(0));
	call->setArg(6, args_ptr);
	call->setArg(7, Imm(arg_count));
	call->setArg(8, Imm(0));
	call->setArg(9, Imm(0));
	call->setRet(0, ret_type);
	call->setRet(1, ret_value);

	dmc.pushStack(Variable{ret_type, ret_value});
}

static bool Emit_Block(DMCompiler& dmc, ProcBlock& block)
{
	dmc.addBlock(block.label);

	for (size_t i=0; i < block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];

		switch (instr.bytes()[0])
		{
		case Bytecode::PUSHI:
			jit_out << "Assembling push integer" << std::endl;
			Emit_PushInteger(dmc, instr.bytes()[1]);
			break;
		case Bytecode::ADD:
		case Bytecode::SUB:
		case Bytecode::MUL:
		case Bytecode::DIV:
			jit_out << "Assembling math op" << std::endl;
			Emit_MathOp(dmc, (Bytecode)instr.bytes()[0]);
			break;
		case Bytecode::SETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling set local" << std::endl;
				Emit_SetLocal(dmc, instr.bytes()[2]);
			}
			break;
		case Bytecode::GETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling get local" << std::endl;
				Emit_GetLocal(dmc, instr.bytes()[2]);
			}
			break;
		case Bytecode::POP:
			jit_out << "Assembling pop" << std::endl;
			Emit_Pop(dmc);
			break;
		case Bytecode::PUSHVAL:
			jit_out << "Assembling push value" << std::endl;
			if (instr.bytes()[1] == DataType::NUMBER) //numbers take up two DWORDs instead of one
			{
				Emit_PushValue(dmc, (DataType)instr.bytes()[1], instr.bytes()[2], instr.bytes()[3]);
			}
			else
			{
				Emit_PushValue(dmc, (DataType)instr.bytes()[1], instr.bytes()[2]);
			}
			break;
		case Bytecode::CALLGLOB:
			jit_out << "Assembling call global" << std::endl;
			Emit_CallGlobal(dmc, instr.bytes()[1], instr.bytes()[2]);
			break;
		case Bytecode::RET:
			Emit_Return(dmc);
			break;
		case Bytecode::DBG_FILE:
		case Bytecode::DBG_LINENO:
			break;
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}

	dmc.endBlock();
	return true;
}


static void compile(std::vector<Core::Proc*> procs)
{
	FILE* fuck = fopen("asm.txt", "w");
	asmjit::FileLogger logger(fuck);
	SimpleErrorHandler eh;
	asmjit::CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);

	DMCompiler dmc(code);

	for (auto& proc : procs)
	{
		Disassembly dis = proc->disassemble();
		byteout_out << "BEGIN " << proc->name << '\n';
		for (Instruction i : dis)
		{
			byteout_out << i.bytes_str() << std::endl;
		}
		byteout_out << "END " << proc->name << '\n';

		size_t locals_count = 0;
		auto blocks = split_into_blocks(dis, dmc, locals_count);

		dmc.addProc(locals_count);
		for (auto& [k, v] : blocks)
		{
			Emit_Block(dmc, v);
		}
		dmc.endProc();
	}

	dmc.finalize();
}

EXPORT const char* ::jit_test(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}

	compile({&Core::get_proc("/proc/jit_test_compiled_proc")});
	return Core::SUCCESS;
}