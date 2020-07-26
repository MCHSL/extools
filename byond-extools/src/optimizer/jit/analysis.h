#pragma once

#include <vector>
#include <optional>

#include "../../dmdism/disassembly.h"
#include "../../third_party/asmjit/asmjit.h"

struct ProcBlock
{
	explicit ProcBlock(const unsigned int o) : offset(o), may_sleep(false) {}
	ProcBlock() : offset(0), may_sleep(false) {}
	std::vector<Instruction> contents;
	unsigned int offset;
	asmjit::Label label;
	bool may_sleep;
};

struct AnalysisResult
{
	std::map<unsigned int, ProcBlock> blocks;
	bool needs_sleep;
	unsigned int argument_count;
	unsigned int local_count;
	unsigned int stack_size;
	std::vector<unsigned int> called_proc_ids;

	unsigned int proc_id;

	AnalysisResult()
		: needs_sleep(false)
		, argument_count(0)
		, local_count(0)
		, stack_size(0)
		, proc_id(0)
	{}
};

std::optional<AnalysisResult> analyze_proc(const Core::Proc& proc);
extern std::map<unsigned int, AnalysisResult> saved_analyses;