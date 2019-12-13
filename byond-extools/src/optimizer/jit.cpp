#include "../core/proc_management.h"
#include "../dmdism/instruction.h"
#include "../dmdism/disassembly.h"
#include "jit.h"
#include "../core/asmjit/asmjit.h"
#include <fstream>
#include <thread>

using namespace asmjit;

std::unordered_map<unsigned int, JitTrace> traced_procs;
JitRuntime rt;

x86::Gp gen_get_context(x86::Compiler& cc)
{
	x86::Gp ctx = cc.newIntPtr();
	cc.mov(ctx, x86::ptr(x86::esp, 4));
	//cc.mov(ctx, x86::ptr((int)Core::current_execution_context_ptr));
	return ctx;
}

x86::Gp gen_get_ctx_args_ptr(x86::Compiler& cc)
{
	x86::Gp ctx = gen_get_context(cc);
	x86::Gp args = cc.newIntPtr();
	cc.mov(args, x86::ptr(ctx, offsetof(ExecutionContext, constants)));
	cc.mov(args, x86::ptr(args, offsetof(ProcConstants, args)));
	return args;
}

x86::Gp gen_get_locals_ptr(x86::Compiler& cc)
{
	x86::Gp ctx = gen_get_context(cc);
	x86::Gp locals = cc.newIntPtr();
	cc.mov(locals, x86::ptr(ctx, offsetof(ExecutionContext, local_variables)));
	return locals;
}

x86::Gp gen_get_local(x86::Compiler& cc, x86::Gp locals, int offset)
{
	x86::Gp localx = cc.newIntPtr();
	cc.mov(localx, x86::ptr(locals, sizeof(Value) * offset + offsetof(Value, valuef)));
	return localx;
}

x86::Gp gen_get_ctx_arg(x86::Compiler& cc, x86::Gp args, int offset)
{
	x86::Gp argx = cc.newIntPtr();
	cc.mov(argx, x86::ptr(args, sizeof(Value) * offset + offsetof(Value, valuef)));
	return argx;
}

void gen_set_local(x86::Compiler& cc, x86::Gp locals, int offset, int type, x86::Gp value)
{
	x86::Gp localx = cc.newIntPtr();
	cc.lea(localx, x86::ptr(locals, sizeof(Value) * offset));
	cc.mov(x86::dword_ptr(localx), type);
	cc.mov(x86::dword_ptr(localx, offsetof(Value, value)), value);
}

void gen_return_from_value(x86::Compiler& cc, x86::Gp addr)
{
	cc.mov(x86::edx, x86::ptr(addr, offsetof(Value, value)));
	cc.mov(x86::eax, x86::ptr(addr, offsetof(Value, type)));
}

void gen_return_value(x86::Compiler& cc, int type, x86::Gp value)
{
	//cc.mov(x86::esi, value);
	cc.mov(x86::edx, value);
	cc.mov(x86::eax, type);

}

x86::Xmm gen_load_number(x86::Compiler& cc, x86::Gp addr)
{
	x86::Xmm reg = cc.newXmm();
	cc.movss(reg, x86::ptr(addr));
	return reg;
}

x86::Gp gen_add_numbers(x86::Compiler& cc, x86::Gp op1, x86::Gp op2)
{
	x86::Xmm fsum = cc.newXmm();
	cc.movd(fsum, op1);
	x86::Xmm fop2 = cc.newXmm();
	cc.movd(fop2, op2);
	cc.addss(fsum, fop2);
	x86::Gp sum = cc.newInt32();
	cc.movd(sum, fsum);
	return sum;
}

x86::Gp gen_push_integer(x86::Compiler& cc, float f)
{
	union funk
	{
		int i;
		float f;
	};
	funk a;
	a.f = f;
	x86::Gp gp = cc.newInt32();
	cc.mov(gp, a.i);
	return gp;
}

x86::Gp gen_args(x86::Compiler& cc)
{
	x86::Gp args = cc.newIntPtr();
	cc.mov(args, x86::ptr(x86::esp, 8));
	return args;
}

x86::Gp gen_get_arg(x86::Compiler& cc, x86::Gp args, int offset)
{
	x86::Gp argx = cc.newIntPtr();
	cc.mov(argx, x86::ptr(args, sizeof(Value) * offset + offsetof(Value, valuef)));
	return argx;
}

std::ofstream jit_out("jit_out.txt");

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	Error err;
};

void consider_jit(Core::Proc p)
{
	jit_out << "Considering " << p.name << std::endl;
	JitTrace jt;
	jt.proc = p;
	traced_procs[p.id] = jt;
}

void record_jit(unsigned int proc_id, Value* args, unsigned int args_len)
{
	JitTrace& jt = traced_procs[proc_id];
	jt.call_count++;
	jit_out << "Recording call of " << jt.proc.name << " (" << jt.call_count << "/10)\n";
	for (int i = 0; i < args_len; i++)
	{
		if (args[i].type != NUMBER)
		{
			jit_out << "Non-number arg, cannot compile\n";
			traced_procs.erase(jt.proc.id);
			return;
		}
	}
	if (jt.call_count >= 10)
	{
		reconsider_jit(jt);
	}
}

void reconsider_jit(JitTrace jt)
{
	jit_out << "Considering " << jt.proc.name << " after trace" << std::endl;
	traced_procs.erase(jt.proc.id);
	jit_out << "Attempting compilation" << std::endl;
	std::thread(jit_compile, jt.proc).detach();
}

void jit_compile(Core::Proc p)
{
	FILE* fuck = fopen("asm.txt", "w");
	FileLogger logger(fuck);
	SimpleErrorHandler eh;
	CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);
	x86::Compiler cc(&code);

	cc.addFunc(FuncSignatureT<int, int, int*, int, int, int, int>(CallConv::kIdHostCDecl));
	Disassembly d = p.disassemble();
	std::vector<x86::Gp> stack;
	std::vector<x86::Gp> local_regs;
	x86::Gp args = gen_args(cc);
	local_regs.push_back(cc.newInt32());
	local_regs.push_back(cc.newInt32());
	local_regs.push_back(cc.newInt32());
	std::unordered_map<unsigned int, std::vector<Label>> jumps;
	for (Instruction& i : d)
	{
		switch (i.bytes()[0])
		{
		case GETVAR:
		{
			if (i.bytes().at(1) == LOCAL)
			{
				jit_out << "Assembling local load\n";
				stack.push_back(local_regs[i.bytes().at(2)]);
			}
			else if (i.bytes().at(1) == ARG)
			{
				jit_out << "Assembling argument load\n";
				stack.push_back(gen_get_arg(cc, args, i.bytes().at(2)));
			}
			break;
		}
		case SETVAR:
		{
			if (i.bytes().at(1) == LOCAL)
			{
				jit_out << "Assembling local store\n";
				cc.mov(local_regs[i.bytes().at(2)], stack[0]);
				stack.erase(stack.begin());
			}
			break;
		}
		case ADD:
		{
			jit_out << "Assembling addition\n";
			stack.push_back(gen_add_numbers(cc, stack[0], stack[1]));
			stack.erase(stack.begin(), stack.begin() + 2);
			break;
		}
		case PUSHI:
		{
			jit_out << "Assembling constant push\n";
			stack.push_back(gen_push_integer(cc, (float)i.bytes().at(1)));
			break;
		}
		case RET:
		{
			jit_out << "Assembling return value\n";
			gen_return_value(cc, 0x2A, stack[0]);
			stack.erase(stack.begin());
			break;
		}
		case DBG_LINENO:
		case DBG_FILE:
		case END:
			break;
		default:
		{
			jit_out << "Cannot compile: Encountered unknown opcode\n";
			return;
		}
		}
	}
	cc.endFunc();

	jit_out << "Finalizing\n";
	int err = cc.finalize();
	if (err)
	{
		jit_out << "Failed to assemble" << std::endl;
		return;
	}

	ProcHook hook;
	err = rt.add(&hook, &code);
	if (err)
	{
		jit_out << "Failed to add to runtime" << std::endl;
		return;
	}
	jit_out << "Compilation successful" << std::endl;
	p.hook(hook);
}