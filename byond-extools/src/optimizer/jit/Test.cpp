#include "Test.h"
#include "DMCompiler.h"

#include <fstream>

using namespace asmjit;
using namespace jit;


static std::ofstream jit_out("jit_out.txt");
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

EXPORT const char* ::jit_test(int n_args, const char** args)
{

	FILE* fuck = fopen("asm.txt", "w");
	asmjit::FileLogger logger(fuck);
	SimpleErrorHandler eh;
	asmjit::CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);

	DMCompiler dmc(code);

	auto proc = dmc.addProc(1);
	auto block = dmc.addBlock();

	// let's do an addition
	auto lhs = dmc.popStack();
	auto rhs = dmc.popStack();
	auto xmm0 = dmc.newXmm();
	auto xmm1 = dmc.newXmm();
	auto res = dmc.newUInt32();

	if (lhs.Value.isImm())
	{
		// dmc.movss(xmm0, lhs.Value.as<Imm>());
	}
	else
	{
		dmc.movd(xmm0, lhs.Value.as<x86::Gp>());
	}

	if (rhs.Value.isImm())
	{
		// dmc.movss(xmm0, lhs.Value.as<Imm>());
	}
	else
	{
		dmc.movd(xmm1, rhs.Value.as<x86::Gp>());
	}

	dmc.addss(xmm0, xmm1);
	dmc.movd(res, xmm0);
	
	dmc.pushStack(jit::Variable{Imm(DataType::NUMBER), res});

	dmc.endBlock();
	dmc.endProc();

	asmjit::String sb;
	Formatter::formatNodeList(sb, FormatOptions::kNoFlags, &dmc);
	logger.log(sb);

	dmc.finalize();

	return "ok";
}