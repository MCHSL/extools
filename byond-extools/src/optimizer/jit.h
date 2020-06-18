#pragma once
#include "../dmdism/disassembly.h"

#ifdef min
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include "../third_party/asmjit/asmjit.h"
#pragma pop_macro("min")
#pragma pop_macro("max")
#else
#include "../third_party/asmjit/asmjit.h"
#endif

#include <vector>

/*
class Block
{
public:
	Block(unsigned int o) : offset(o) { contents = {}; }
	Block() { offset = 0; contents = {}; }
	std::vector<Instruction> contents;
	unsigned int offset;
	asmjit::Label label;
	asmjit::Label label2;
};
*/

void jit_compile(std::vector<Core::Proc*> procs);