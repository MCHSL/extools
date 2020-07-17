#pragma once

#include "Test.h"
#include "DMCompiler.h"
#include "JitContext.h"

#include "../../core/core.h"
#include "../../dmdism/instruction.h"
#include "../../dmdism/disassembly.h"

struct JitTrace
{
	std::vector<std::vector<DataType>> arg_types;
};