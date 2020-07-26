#include <unordered_set>

#include "analysis.h"
#include "DMCompiler.h"
#include "jit_runtime.h"
#include "../../core/core.h"
#include "translation.h"

using namespace dmjit;

static std::unordered_set<unsigned int> unjitted_procs;

static unsigned int shift(const unsigned int n)
{
	return static_cast<unsigned int>(log2(n));
}

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

static void print_jit_runtime(const char* err)
{
	Core::alert_dd(err);
}

static unsigned int add_strings(unsigned int str1, unsigned int str2)
{
	return Core::GetStringId(Core::GetStringFromId(str1) + Core::GetStringFromId(str2));
}

static std::string get_parent_type(const std::string& typepath)
{
	Value path = TextToPath(Core::GetStringId(typepath));
	if (path.type == MOB_TYPEPATH)
	{
		path.value = *MobTableIndexToGlobalTableIndex(path.value);
	}
	const TType* type = GetTypeById(path.value);
	const unsigned int parent_type = type->parentTypeIdx;
	return Core::GetStringFromId(GetTypeById(parent_type)->path);
}

static Core::Proc* find_parent_proc(const unsigned int proc_id)
{
	const Core::Proc& proc = Core::get_proc(proc_id);
	std::string proc_path = proc.name;
	const std::string& proc_name = proc.simple_name;

	const size_t proc_pos = proc_path.rfind('/');
	if (proc_pos != std::string::npos)
	{
		proc_path.erase(proc_pos);
	}
	while (proc_path.find('/') != std::string::npos)
	{
		proc_path = get_parent_type(proc_path);
		Core::Proc* p = Core::try_get_proc(proc_path + "/" + proc_name);
		if (p)
		{
			return p;
		}

	}
	return nullptr;
}

static unsigned int translate_proc_name(unsigned int datum_type, unsigned int datum_id, unsigned int name)
{
	Value type = Value((DataType)datum_type, datum_id).get("type");
	if (type.type == MOB_TYPEPATH)
	{
		type.value = *MobTableIndexToGlobalTableIndex(type.value);
	}
	std::string stringy_type = Core::GetStringFromId(GetTypeById(type.value)->path);
	const std::string proc_name = Core::GetStringFromId(name);
	while (stringy_type.find('/') != std::string::npos)
	{
		const Core::Proc* proc = Core::try_get_proc(stringy_type + "/" + proc_name);
		if (proc)
		{
			return proc->id;
		}
		stringy_type = get_parent_type(stringy_type);
	}
	return -1;
}

static void* check_is_jitted(unsigned int proc_id)
{
	auto jit_it = jitted_procs.find(proc_id);
	if (jit_it != jitted_procs.end())
	{
		return jit_it->second.code_base;
	}
	return nullptr;
}

static void add_unjitted_proc(unsigned int proc_id)
{
	const auto ret = unjitted_procs.emplace(proc_id);
	if (ret.second)
	{
		const Core::Proc& proc = Core::get_proc(proc_id);
		//Core::Alert(proc.name);
		proc.jit();
	}
}

class DMTranslator
{
public:
	DMTranslator(DMCompiler& compiler, AnalysisResult res) : dmc(compiler), analysis(res) {}

	[[nodiscard]] bool compile_block(const ProcBlock& block) const
	{
		dmc.addBlock(block.label, block.may_sleep);
		for (size_t i = 0; i < block.contents.size(); i++)
		{
			const Instruction& instr = block.contents[i];
			const std::vector<uint32_t>& instr_bytes = instr.bytes();

			switch (instr_bytes[0])
			{
			// Stack manipulation
			case PUSHI:
				push_integer(instr_bytes[1]);
				break;
			case PUSHVAL:
				if (instr_bytes[1] == NUMBER) // Numbers take up two DWORDs instead of one
				{
					push_value((DataType)instr_bytes[1], instr_bytes[2] << 16 | instr_bytes[3]);
				}
				else
				{
					push_value((DataType)instr_bytes[1], instr_bytes[2]);
				}
				break;
			case POP:
				dmc.popStack();
				break;

			// Logic stuff
			case TEST:
				test_stack_top();
				break;
			case GETFLAG:
				get_flag();
				break;
			case NOT:
				logical_not();
				break;


				
			// Binary operations	
			case ADD:
			case SUB:
			case MUL:
			case DIV:
				dmc.setInlineComment("binary operation");
				binary_op((Bytecode)instr_bytes[0]);
				break;



			// Comparing variables
			case Bytecode::TEQ:
			case Bytecode::TNE:
			case Bytecode::TL:
			case Bytecode::TLE:
			case Bytecode::TG:
			case Bytecode::TGE:
				jit_out << "Assembling comparison" << std::endl;
				compare_values((Bytecode)instr_bytes[0]);
				break;



			// Jumping
			case Bytecode::JMP:
			case Bytecode::JMP2:
				jit_out << "Assembling jump" << std::endl;
				dmc.jmp(analysis.blocks.at(instr_bytes[1]).label);
				break;
			case Bytecode::JUMP_FALSE:
			case Bytecode::JUMP_FALSE2:
			case Bytecode::JUMP_TRUE:
			case Bytecode::JUMP_TRUE2:
				jit_out << "Assembling conditional jump" << std::endl;
				conditional_jump((Bytecode)instr_bytes[0], analysis.blocks.at(instr_bytes[1]).label);
				break;
			case Bytecode::JMP_AND:
			case Bytecode::JMP_OR:
				jit_out << "Assembling chain jump" << std::endl;
				conditional_chain_jump((Bytecode)instr_bytes[0], analysis.blocks.at(instr_bytes[1]).label);
				break;

				

			// Variable access
			case SETVAR:
				switch (instr.bytes()[1])
				{
				case LOCAL:
					jit_out << "Assembling set local" << std::endl;
					dmc.setInlineComment("set local");
					dmc.setLocal(instr.bytes()[2], dmc.popStack());
					break;
				case DOT:
					jit_out << "Assembling set dot" << std::endl;
					dmc.setInlineComment("set dot");
					dmc.setDot(dmc.popStack());
					break;
				case SUBVAR:
					jit_out << "Assembling write to field" << std::endl;
					write_to_field(instr);
					break;
				default:
					if (instr_bytes[1] > 64000)
					{
						Core::Alert("Failed to assemble setvar");
					}
					else
					{
						jit_out << "Assembling set cached field" << std::endl;
						write_to_cached_field(instr_bytes[1]);
					}
					break;
				}
				break;
			case GETVAR:
				switch (instr.bytes()[1])
				{
				case LOCAL:
					dmc.pushStackRaw(dmc.getLocal(instr.bytes()[2]));
					break;
				case ARG:
					dmc.pushStackRaw(dmc.getArg(instr.bytes()[2]));
					break;
				case SRC:
					dmc.pushStackRaw(dmc.getSrc());
					break;
				case DOT:
					dmc.pushStackRaw(dmc.getDot());
					break;
				case SUBVAR:
					jit_out << "Assembling field read" << std::endl;
					read_from_field(instr);
					break;
				case WORLD:
					dmc.pushStack(imm(DataType::WORLD_D), imm(0));
					break;
				default:
					if (instr.bytes()[1] > 64000)
					{
						Core::Alert("Failed to assemble getvar");
					}
					else
					{
						jit_out << "Assembling get cached field" << std::endl;
						read_from_cached_field(instr_bytes[1]);
					}
					break;
				}
				break;



			// Proc calls
			case Bytecode::CALLGLOB:
				jit_out << "Assembling call global" << std::endl;
				dmc.setInlineComment("call global proc");
				call_global(instr_bytes[1], instr_bytes[2]);
				break;
			case Bytecode::CALL:
			case Bytecode::CALLNR:
				jit_out << "Assembling call" << std::endl;
				dmc.setInlineComment("call proc");
				call(instr);
				break;
			case Bytecode::CALLPARENT:
				jit_out << "Assembling default call parent" << std::endl;
				dmc.setInlineComment("call default parent");
				call_parent(analysis.proc_id);
				break;

				
				
			// Returning and yielding
			case RET:
				dmc.doReturn();
				break;
			case END:
			{
				Variable dot = dmc.getDot();
				dmc.pushStackRaw(dot);
				dmc.doReturn();
				break;
			}
			case SLEEP:
				dmc.doSleep();
				break;


			// Other
			case DBG_FILE:
			case DBG_LINENO:
				break;
			}
		}
		
		dmc.endBlock();
		return true;
	}

protected:
	void push_integer(const float integer) const
	{
		dmc.pushStack(imm(NUMBER), imm(EncodeFloat(integer)));
	}

	void push_value(DataType type, uint32_t value) const
	{
		dmc.pushStack(imm(type), imm(value));
	}

	void test_stack_top() const
	{
		const auto val = dmc.popStack();
		const auto truthy = dmc.newLabel();
		const auto falsey = dmc.newLabel();
		const auto done = dmc.newLabel();
		check_truthiness(val, truthy, falsey);
		dmc.bind(falsey);
		dmc.unsetFlag();
		dmc.jmp(done);
		dmc.bind(truthy);
		dmc.setFlag();
		dmc.bind(done);
	}

	void read_from_field(const Instruction& instr) const
	{
		const auto& base = instr.acc_base;
		Variable base_var;
		switch (base.first)
		{
		case ARG:
			base_var = dmc.getArg(base.second);
			break;
		case LOCAL:
			base_var = dmc.getLocal(base.second);
			break;
		case SRC:
			base_var = dmc.getSrc();
			break;
		case 0:
			base_var = dmc.getCached();
			break;
		default:
			Core::Alert("Unknown access modifier in jit get_variable");
			break;
		}

		dmc.setCached(base_var);

		for (unsigned int name : instr.acc_chain)
		{
			InvokeNode* call;
			dmc.invoke(&call, reinterpret_cast<uint64_t>(GetVariable), FuncSignatureT<Type::I64, int, int, int>());
			call->setArg(0, base_var.Type);
			call->setArg(1, base_var.Value);
			call->setArg(2, imm(name));
			call->setRet(0, base_var.Type);
			call->setRet(1, base_var.Value);
		}

		dmc.pushStackRaw(base_var);
	}

	void write_to_field(const Instruction& instr) const
	{
		const Variable new_value = dmc.popStack();
		const auto& base = instr.acc_base;
		Variable base_var;
		switch (base.first)
		{
		case ARG:
			base_var = dmc.getArg(base.second);
			break;
		case LOCAL:
			base_var = dmc.getLocal(base.second);
			break;
		case SRC:
			base_var = dmc.getSrc();
			break;
		case 0:
			base_var = dmc.getCached();
			break;
		default:
			Core::Alert("Unknown access modifier in jit get_variable");
			break;
		}

		dmc.setCached(base_var);

		for (unsigned int i = 0; i < instr.acc_chain.size() - 1; i++)
		{
			const unsigned int name = instr.acc_chain.at(i);
			auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
			call->setArg(0, base_var.Type);
			call->setArg(1, base_var.Value);
			call->setArg(2, imm(name));
			call->setRet(0, base_var.Type);
			call->setRet(1, base_var.Value);
		}

		const unsigned int final_field_name = instr.acc_chain.back();
		auto* call = dmc.call((uint64_t)SetVariable, FuncSignatureT<void, int, int, int, int, int>());
		call->setArg(0, base_var.Type);
		call->setArg(1, base_var.Value);
		call->setArg(2, imm(final_field_name));
		call->setArg(3, new_value.Type);
		call->setArg(4, new_value.Value);
	}

	void read_from_cached_field(const unsigned int name) const
	{
		Variable result;
		const Variable cached = dmc.getCached();
		auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<asmjit::Type::I64, int, int, int>());
		call->setArg(0, cached.Type);
		call->setArg(1, cached.Value);
		call->setArg(2, imm(name));
		call->setRet(0, result.Type);
		call->setRet(1, result.Value);
		dmc.pushStackRaw(result);
	}

	void write_to_cached_field(const unsigned int name) const
	{
		const Variable value = dmc.popStack();
		const Variable cached = dmc.getCached();
		auto* call = dmc.call((uint64_t)SetVariable, FuncSignatureT<void, int, int, int, int, int>());
		call->setArg(0, cached.Type);
		call->setArg(1, cached.Value);
		call->setArg(2, imm(name));
		call->setArg(3, value.Type);
		call->setArg(4, value.Value);
	}

	void call_global(uint32_t arg_count, uint32_t proc_id) const
	{
		const auto args_ptr = alloc_arguments_from_stack(arg_count);
		Variable ret;

		auto proc_it = jitted_procs.find(proc_id);
		if (proc_it != jitted_procs.end())
		{
			void* code_base = proc_it->second.code_base;
			InvokeNode* call;
			dmc.invoke(&call, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<asmjit::Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
			call->setArg(0, imm(code_base));
			call->setArg(1, imm(arg_count));
			call->setArg(2, args_ptr);
			call->setArg(3, imm(0));
			call->setArg(4, imm(0));
			const Variable usr = dmc.getFrameEmbeddedValue(offsetof(ProcStackFrame, usr));
			call->setArg(5, usr.Type);
			call->setArg(6, usr.Value);
			call->setArg(7, dmc.getJitContext());
			call->setRet(0, ret.Type);
			call->setRet(1, ret.Value);
		}
		else
		{
			InvokeNode* call;
			dmc.invoke(&call, reinterpret_cast<uint64_t>(CallGlobalProc), FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, int, int*, int, int, int>());
			call->setArg(0, Imm(0));
			call->setArg(1, Imm(0));
			call->setArg(2, Imm(2));
			call->setArg(3, Imm(proc_id));
			call->setArg(4, Imm(0));
			call->setArg(5, Imm(0));
			call->setArg(6, Imm(0));
			call->setArg(7, args_ptr);
			call->setArg(8, Imm(arg_count));
			call->setArg(9, Imm(0));
			call->setArg(10, Imm(0));
			call->setRet(0, ret.Type);
			call->setRet(1, ret.Value);
		}
		dmc.pushStackRaw(ret);
	}

	void call(const Instruction& instr) const
	{
		const auto& bytes = instr.bytes();
		const unsigned int proc_selector = bytes.end()[-3];
		const unsigned int proc_identifier = bytes.end()[-2];
		const unsigned int arg_count = bytes.end()[-1];

		const auto args_ptr = alloc_arguments_from_stack(arg_count);


		Variable result;

		const auto& base = instr.acc_base;

		Variable base_var;
		switch (base.first)
		{
		case ARG:
			base_var = dmc.getArg(base.second);
			break;
		case LOCAL:
			base_var = dmc.getLocal(base.second);
			break;
		case SRC:
			base_var = dmc.getSrc();
			break;
		case 0:
			base_var = dmc.getCached();
			break;
		default:
			if (base.first > 64000)
			{
				Core::Alert("Unknown access modifier in jit get_variable");
			}
			break;
		}

		dmc.setCached(base_var);

		if (!instr.acc_chain.empty())
		{
			for (unsigned int i = 0; i < instr.acc_chain.size() - 1; i++)
			{
				const unsigned int name = instr.acc_chain.at(i);
				auto* call = dmc.call((uint64_t)GetVariable, FuncSignatureT<Type::I64, int, int, int>());
				call->setArg(0, base_var.Type);
				call->setArg(1, base_var.Value);
				call->setArg(2, imm(name));
				call->setRet(0, base_var.Type);
				call->setRet(1, base_var.Value);
			}
		}

		const auto usr = dmc.getUsr();
		const auto proc_id = dmc.newUInt32("proc_id");

		if (proc_selector == ProcSelector::SRC_PROC_SPEC)
		{
			InvokeNode* call;
			dmc.invoke(&call, (uint64_t)translate_proc_name, FuncSignatureT<unsigned int, unsigned int, unsigned int, unsigned int>());
			call->setArg(0, base_var.Type);
			call->setArg(1, base_var.Value);
			call->setArg(2, imm(proc_identifier));
			call->setRet(0, proc_id);
		}
		else
		{
			dmc.mov(proc_id, imm(proc_identifier));
		}

		const auto not_jitted = dmc.newLabel();
		const auto done = dmc.newLabel();

		const auto code_base = dmc.newUIntPtr("code_base");

		InvokeNode* check_jit;
		dmc.invoke(&check_jit, reinterpret_cast<uint64_t>(check_is_jitted), FuncSignatureT<void*, unsigned int>());
		check_jit->setArg(0, proc_id);
		check_jit->setRet(0, code_base);

		dmc.test(code_base, code_base);
		dmc.je(not_jitted);
		InvokeNode* jitcall;
		dmc.invoke(&jitcall, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
		jitcall->setArg(0, code_base);
		jitcall->setArg(1, imm(arg_count));
		jitcall->setArg(2, args_ptr);
		jitcall->setArg(3, base_var.Type);
		jitcall->setArg(4, base_var.Value);
		jitcall->setArg(5, usr.Type);
		jitcall->setArg(6, usr.Value);
		jitcall->setArg(7, dmc.getJitContext());
		jitcall->setRet(0, result.Type);
		jitcall->setRet(1, result.Value);

		dmc.jmp(done);
		dmc.bind(not_jitted);
		auto* add_unjitted = dmc.call((uint64_t)add_unjitted_proc, FuncSignatureT<void, unsigned int>());
		add_unjitted->setArg(0, proc_id);
		auto* call = dmc.call((uint64_t)CallGlobalProc, FuncSignatureT<Type::I64, int, int, int, int, int, int, Value*, int, int, int, int>());
		call->setArg(0, usr.Type);
		call->setArg(1, usr.Value);
		call->setArg(2, imm(0x2));
		call->setArg(3, proc_id);
		call->setArg(4, imm(0));
		call->setArg(5, base_var.Type);
		call->setArg(6, base_var.Value);
		call->setArg(7, args_ptr);
		call->setArg(8, imm(arg_count));
		call->setArg(9, imm(0));
		call->setArg(10, imm(0));
		call->setRet(0, result.Type);
		call->setRet(1, result.Value);
		dmc.bind(done);
		dmc.pushStackRaw(result);
	}

	void jump(const Label& destination) const
	{
		dmc.jmp(destination);
	}

	void check_truthiness(const Variable& val, const Label& truthy, const Label& falsey) const
	{
		dmc.cmp(val.Value, imm(0)); // If the value is not 0, then it must be truthy.
		dmc.jne(truthy);
		dmc.cmp(val.Type.r8Lo(), imm(0)); // If it has a value of zero but is not NULL, a string or a number, it's truthy.
		dmc.je(falsey);
		dmc.cmp(val.Type.r8Lo(), imm(STRING));
		dmc.je(falsey);
		dmc.cmp(val.Type.r8Lo(), imm(NUMBER));
		dmc.je(falsey);
		dmc.jmp(truthy);
	}

	void compare_values(const Bytecode comp) const
	{
		const auto [lhs, rhs] = dmc.popStack<2>();
		const auto stack_crap = dmc.pushStack(imm(NUMBER), imm(0));

		if (comp == Bytecode::TEQ || comp == Bytecode::TNE)
		{
			const auto not_equal = dmc.newLabel();
			const auto done = dmc.newLabel();
			dmc.cmp(lhs.Type.r8Lo(), rhs.Type.r8Lo());
			dmc.jne(not_equal);
			dmc.cmp(lhs.Value, rhs.Value);
			dmc.jne(not_equal);
			dmc.setFlag();
			dmc.mov(stack_crap.Value, imm(0x3F800000));
			dmc.jmp(done);
			dmc.bind(not_equal);
			dmc.unsetFlag();
			dmc.mov(stack_crap.Value, imm(0));
			dmc.bind(done);
		}
		else
		{
			const auto does_not_pass = dmc.newLabel();
			const auto done = dmc.newLabel();

			// We're only going to allow comparing numbers.
			dmc.cmp(lhs.Type.r8Lo(), imm(NUMBER));
			dmc.jne(does_not_pass);
			dmc.cmp(rhs.Type.r8Lo(), imm(NUMBER));
			dmc.jne(does_not_pass);
			dmc.cmp(lhs.Value, rhs.Value);
			switch (comp)
			{
			case Bytecode::TL:
				dmc.jge(does_not_pass);
				break;
			case Bytecode::TLE:
				dmc.jg(does_not_pass);
				break;
			case Bytecode::TG:
				dmc.jle(does_not_pass);
				break;
			case Bytecode::TGE:
				dmc.jl(does_not_pass);
				break;
			default:
				Core::Alert("UNKNOWN COMPARISON");
				dmc.jmp(does_not_pass);
				break;
			}
			dmc.setFlag();
			dmc.mov(stack_crap.Value, imm(0x3F800000));
			dmc.jmp(done);
			dmc.bind(does_not_pass);
			dmc.unsetFlag();
			dmc.mov(stack_crap.Value, imm(0));
			dmc.bind(done);
		}
	}

	void conditional_jump(const Bytecode cond, const Label& target) const
	{
		const auto flag = dmc.getFlag();
		dmc.test(flag, flag);
		switch (cond)
		{
		case Bytecode::JUMP_FALSE:
		case Bytecode::JUMP_FALSE2:
			dmc.jz(target);
			break;
		case Bytecode::JUMP_TRUE:
		case Bytecode::JUMP_TRUE2:
			dmc.jnz(target);
			break;
		default:
			break; // Stop complaining, resharper
		}
	}

	void conditional_chain_jump(const Bytecode cond, const Label& target) const
	{
		const auto lhs_result = dmc.popStack();
		const auto truthy = dmc.newLabel();
		const auto falsey = dmc.newLabel();
		const auto done = dmc.newLabel();
		check_truthiness(lhs_result, truthy, falsey);
		dmc.bind(falsey);
		if (cond == JMP_AND)
		{
			dmc.pushStackDirect(Variable{ imm(0).as<x86::Gp>(), imm(0).as<x86::Gp>() });
			dmc.jmp(target);
		}
		else
		{
			dmc.jmp(done);
		}
		dmc.bind(truthy);
		if (cond == JMP_OR)
		{
			dmc.pushStackDirect(Variable{ imm(NUMBER).as<x86::Gp>(), imm(0x3F800000).as<x86::Gp>() });
			dmc.jmp(target);
		}
		dmc.bind(done);
	}

	void call_parent(const unsigned int proc_id) const
	{
		const Core::Proc* const parent_proc = find_parent_proc(proc_id);
		Core::Alert(parent_proc->name);
		if (!parent_proc)
		{
			Core::Alert("Failed to locate parent proc");
		}
		const auto result = dmc.pushStack();

		const auto usr = dmc.getUsr();
		const auto src = dmc.getSrc();

		const auto not_jitted = dmc.newLabel();
		const auto done = dmc.newLabel();

		const auto code_base = dmc.newUIntPtr("code_base");

		const unsigned int arg_count = dmc.getArgCount();

		x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
		args.setSize(sizeof(uint32_t));

		for (unsigned int i = 0; i < arg_count; i++)
		{
			Variable var = dmc.getArg(i);

			args.setOffset((uint64_t)i * sizeof(Value) + offsetof(Value, type));
			dmc.mov(args, var.Type.as<x86::Gp>());

			args.setOffset((uint64_t)i * sizeof(Value) + offsetof(Value, value));
			dmc.mov(args, var.Value.as<x86::Gp>());
		}
		args.resetOffset();
		const x86::Gp args_ptr = dmc.newUIntPtr("call_parent_args_ptr");
		dmc.lea(args_ptr, args);

		InvokeNode* check_jit;
		dmc.invoke(&check_jit, reinterpret_cast<uint64_t>(check_is_jitted), FuncSignatureT<void*, unsigned int>());
		check_jit->setArg(0, imm(parent_proc->id));
		check_jit->setRet(0, code_base);

		dmc.test(code_base, code_base);
		dmc.jz(not_jitted);
		InvokeNode* jitcall;
		dmc.invoke(&jitcall, reinterpret_cast<uint64_t>(JitEntryPoint), FuncSignatureT<asmjit::Type::I64, void*, int, Value*, int, int, int, int, JitContext*>());
		jitcall->setArg(0, code_base);
		jitcall->setArg(1, imm(arg_count));
		jitcall->setArg(2, args_ptr);
		jitcall->setArg(3, src.Type);
		jitcall->setArg(4, src.Value);
		jitcall->setArg(5, usr.Type);
		jitcall->setArg(6, usr.Value);
		jitcall->setArg(7, dmc.getJitContext());
		jitcall->setRet(0, result.Type);
		jitcall->setRet(1, result.Value);

		dmc.jmp(done);
		dmc.bind(not_jitted);
		auto* add_unjitted = dmc.call((uint64_t)add_unjitted_proc, FuncSignatureT<void, unsigned int>());
		add_unjitted->setArg(0, imm(parent_proc->id));
		auto* call = dmc.call((uint64_t)CallGlobalProc, FuncSignatureT<asmjit::Type::I64, int, int, int, int, int, int, Value*, int, int, int, int>());
		call->setArg(0, usr.Type);
		call->setArg(1, usr.Value);
		call->setArg(2, imm(0x2));
		call->setArg(3, imm(parent_proc->id));
		call->setArg(4, imm(0));
		call->setArg(5, src.Type);
		call->setArg(6, src.Value);
		call->setArg(7, args_ptr);
		call->setArg(8, imm(arg_count));
		call->setArg(9, imm(0));
		call->setArg(10, imm(0));
		call->setRet(0, result.Type);
		call->setRet(1, result.Value);
		dmc.bind(done);

	}

	void binary_op(Bytecode op_type) const
	{
		auto [lhs, rhs] = dmc.popStack<2>();
		const Variable result = dmc.pushStack(Imm(NUMBER), Imm(0));
		const auto type_comparator = dmc.newUInt32("type_comparator");
		const auto done_adding_strings = dmc.newLabel();
		const auto invalid_arguments = dmc.newLabel();

		// At least one of the operands to cmp needs to be a register.
		if (!lhs.Type.isReg())
		{
			dmc.movzx(type_comparator, lhs.Type);
			dmc.cmp(type_comparator.r8Lo(), rhs.Type.r8Lo());
		}
		else if (!rhs.Type.isReg())
		{
			dmc.mov(type_comparator, rhs.Type);
			dmc.cmp(type_comparator.r8Lo(), lhs.Type.r8Lo());
		}
		else
		{
			dmc.cmp(lhs.Type.r8Lo(), rhs.Type.r8Lo());
		}
		dmc.jne(invalid_arguments);

		if (op_type == ADD)
		{
			const auto notstring = dmc.newLabel();

			dmc.mov(type_comparator, lhs.Type);
			dmc.cmp(type_comparator.r8Lo(), Imm(STRING));
			dmc.jne(notstring);

			dmc.mov(type_comparator, rhs.Type);
			dmc.cmp(type_comparator.r8Lo(), Imm(STRING));
			dmc.jne(notstring);

			do_string_addition(lhs, rhs, result);
			dmc.jmp(done_adding_strings);

			dmc.bind(notstring);
		}

		do_math_operation(op_type, lhs, rhs, result);
		dmc.jmp(done_adding_strings);
		dmc.bind(invalid_arguments);
		abort_proc("Runtime in JIT compiled function: Invalid operand types for binary operation");
		dmc.bind(done_adding_strings);
	}

	void get_flag() const
	{
		const auto done = dmc.newLabel();
		const auto flag = dmc.getFlag();
		const auto stack_crap = dmc.pushStack(imm(NUMBER), imm(0));
		dmc.test(flag, flag);
		dmc.jz(done); // we push FALSE by default so no need to update it.
		dmc.mov(stack_crap.Value, imm(0x3f800000)); // Floating point representation of 1 (TRUE)
		dmc.bind(done);
	}

	void logical_not() const
	{
		const auto val = dmc.popStack();
		const auto result = dmc.pushStack(imm(NUMBER), imm(0));
		const auto truthy = dmc.newLabel();
		const auto falsey = dmc.newLabel();
		check_truthiness(val, truthy, falsey);
		dmc.bind(falsey);
		dmc.mov(result.Value, imm(0x3f800000));
		dmc.bind(truthy); // We push FALSE by default so no need to do anything
	}

	void do_string_addition(const Variable lhs, const Variable rhs, const Variable result) const
	{
		InvokeNode* call;
		dmc.invoke(&call, reinterpret_cast<uint32_t>(add_strings), FuncSignatureT<unsigned int, unsigned int, unsigned int>());
		call->setArg(0, lhs.Value);
		call->setArg(1, rhs.Value);
		call->setRet(0, result.Value);
		dmc.mov(result.Type, Imm(STRING));
	}

	void do_math_operation(const Bytecode op_type, const Variable lhs, const Variable rhs, const Variable result) const
	{
		const auto xmm0 = dmc.newXmm("lhs");
		const auto xmm1 = dmc.newXmm("rhs");

		if (lhs.Value.isImm())
		{
			const auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(lhs.Value.as<Imm>().value()));
			dmc.movd(xmm0, data);
		}
		else
		{
			dmc.movd(xmm0, lhs.Value.as<x86::Gp>());
		}

		if (rhs.Value.isImm())
		{
			const auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(rhs.Value.as<Imm>().value()));
			dmc.movd(xmm1, data);
		}
		else
		{
			dmc.movd(xmm1, rhs.Value.as<x86::Gp>());
		}

		switch (op_type)
		{
		case ADD:
			dmc.addss(xmm0, xmm1);
			break;
		case SUB:
			dmc.subss(xmm0, xmm1);
			break;
		case MUL:
			dmc.mulss(xmm0, xmm1);
			break;
		case DIV:
			dmc.divss(xmm0, xmm1);
			break;
		default:
			Core::Alert("Unknown math operation");
		}

		dmc.mov(result.Type, NUMBER);
		dmc.movd(result.Value, xmm0);
	}

	void abort_proc(const char* msg) const
	{
		InvokeNode* call;
		dmc.invoke(&call, reinterpret_cast<uint32_t>(print_jit_runtime), FuncSignatureT<void, char*>());
		call->setArg(0, imm(msg));
		dmc.pushStack(imm(NULL_D), imm(0));
		dmc.doReturn(true);
	}

	[[nodiscard]] x86::Gp alloc_arguments_from_stack(unsigned int arg_count) const
	{
		x86::Mem args = dmc.newStack(sizeof(Value) * arg_count, 4);
		args.setSize(sizeof(uint32_t));
		if (arg_count == 0xFFFF)
		{
			arg_count = 1; // Argument count of 0xFFFF means there's a list of args on the stack and we need to pass that.
		}
		uint32_t arg_i = arg_count;
		while (arg_i--)
		{
			Variable var = dmc.popStack();

			args.setOffset((uint64_t)arg_i * sizeof(Value) + offsetof(Value, type));
			if (var.Type.isImm())
			{
				//auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Type.as<Imm>().value()));
				dmc.mov(args, var.Type.as<Imm>());
			}
			else
			{
				dmc.mov(args, var.Type.as<x86::Gp>());
			}

			args.setOffset((uint64_t)arg_i * sizeof(Value) + offsetof(Value, value));
			if (var.Value.isImm())
			{
				//auto data = dmc.newFloatConst(ConstPool::kScopeLocal, DecodeFloat(var.Value.as<Imm>().value()));
				dmc.mov(args, var.Value.as<Imm>());
			}
			else
			{
				dmc.mov(args, var.Value.as<x86::Gp>());
			}
		}
		args.resetOffset();
		x86::Gp args_ptr = dmc.newUIntPtr("call_args_ptr");
		dmc.lea(args_ptr, args);
		return args_ptr;
	}
	

private:
	DMCompiler& dmc;
	const AnalysisResult analysis;
};

static std::ofstream bytecode_out("newbytecode.txt");
static asmjit::JitRuntime rt;

class SimpleErrorHandler : public asmjit::ErrorHandler
{
public:
	void handleError(asmjit::Error err, const char* message, asmjit::BaseEmitter* origin) override
	{
		this->err = err;
		jit_out << message << "\n";
	}

	asmjit::Error err = kErrorOk;
};

static FILE* fuck;
static SimpleErrorHandler eh;
static JitRuntime jr;
static CodeHolder code;

bool one_time_init = false;

bool compile(const Core::Proc& proc)
{
	if(!one_time_init)
	{
		one_time_init = true;
		fopen_s(&fuck, "newasm.txt", "w");
		code.init(rt.environment());
		code.setErrorHandler(&eh);
	}

	
	FileLogger logger(fuck);
	logger.addFlags(FormatOptions::kFlagRegCasts | FormatOptions::kFlagExplainImms | FormatOptions::kFlagDebugPasses | FormatOptions::kFlagDebugRA | FormatOptions::kFlagAnnotations);
	code.setLogger(&logger);

	DMCompiler dmc(code);

	Disassembly dis = proc.disassemble();
	bytecode_out << "BEGIN " << proc.name << '\n';
	for (const Instruction& i : dis)
	{
		bytecode_out << i.bytes_str() << std::endl;
	}
	bytecode_out << "END " << proc.name << '\n';

	auto opt_result = analyze_proc(proc);

	if (!opt_result)
	{
		Core::Alert("Analysis for " + proc.name + " failed.");
		return false;
	}

	auto result = *opt_result;

	for (auto& [k, v] : result.blocks)
	{
		v.label = dmc.newLabel();
	}

	const ProcNode* node = dmc.addProc(result.local_count, result.argument_count, result.needs_sleep);
	const auto dmt = DMTranslator(dmc, result);

	for (auto& [k, v] : result.blocks)
	{
		if (!dmt.compile_block(v))
		{
			fflush(fuck);
			return false;
		}
	}
	dmc.endProc();

	dmc.finalize();
	void* code_base = nullptr;
	jr.add(&code_base, &code);

	jitted_procs.emplace(proc.id, JittedInfo(code_base, false));
	jit_out << proc.name << ": " << code_base << std::endl;
	
	fflush(fuck);
	return true;
}

void* compile_one(const Core::Proc& proc)
{
	if (!compile(proc))
	{
		return nullptr;
	}
	return jitted_procs[proc.id].code_base; //todo
}