#include "../core/core.h"
#include "../dmdism/instruction.h"
#include "../dmdism/disassembly.h"
#include <set>
#include "../third_party/asmjit/asmjit.h"
#include <fstream>
#include "jit.h"
#include <algorithm>
#include <stack>
#include <variant>
#include <array>

using namespace asmjit;

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

struct RegisterAllocator;

struct Register
{
	~Register();

	Register(RegisterAllocator* allocator, size_t index, uint32_t cookie)
		: allocator(allocator)
		, index(index)
		, cookie(cookie)
	{}

	Register(const Register&) = delete;
	Register& operator=(const Register&) = delete;

	Register(Register&& src) noexcept
		: allocator(std::exchange(src.allocator, nullptr))
		, index(std::exchange(src.index, 0))
		, cookie(std::exchange(src.cookie, 0))
	{}

	Register& operator=(Register&& src) noexcept
	{
		allocator = std::exchange(src.allocator, nullptr);
		index = std::exchange(src.index, 0);
		cookie = std::exchange(src.cookie, 0);
		return *this;
	}

	x86::Gp Get();

	operator x86::Gp() { return Get(); }

	RegisterAllocator* allocator;
	size_t index;
	uint32_t cookie;
};

struct RegisterAllocator
{
	RegisterAllocator()
		: registers{{x86::eax, 0}, {x86::ecx, 0}, {x86::edx, 0}, {x86::ebx, 0}, {x86::ebp, 0}, {x86::esi, 0}, {x86::edi, 0}}
		, next_cookie(1)
	{

	}

	Register New(x86::Assembler& ass)
	{
		auto it = std::find_if(registers.begin(), registers.end(),
			[](const std::pair<x86::Gp, uint32_t>& pair) { return pair.second == 0; });

		if (it == registers.end())
			throw;

		if (RegisterNeedsRestoring(it->first))
		{
			if (std::find(pushed.begin(), pushed.end(), it->first) == pushed.end())
			{
				ass.setInlineComment( "RegisterAllocator::New" );
				ass.push(it->first);
				pushed.push_back(it->first);
			}
		}

		uint32_t cookie = next_cookie++;
		it->second = cookie;
		return Register{this, static_cast<size_t>(it - registers.begin()), cookie};
	}

	void Release(Register& reg)
	{
		if (this != reg.allocator)
			throw;

		if (reg.index >= registers.size())
			throw;

		auto& pair = registers[reg.index];

		if (reg.cookie == pair.second)
		{
			pair.second = 0;
		}
	}

	x86::Gp Get(Register& reg)
	{
		if (this != reg.allocator)
			throw;

		if (reg.index >= registers.size())
			throw;

		auto& pair = registers[reg.index];

		if (reg.cookie != pair.second)
			throw;

		return pair.first;
	}

	void Flush(x86::Assembler& ass)
	{
		for (auto& pair : registers)
		{
			pair.second = 0;
		}

		ass.setInlineComment( "RegisterAllocator::Flush" );
		for (auto it = pushed.rbegin(); it != pushed.rend(); ++it)
		{
			ass.pop(*it);
		}
		pushed.clear();
		ass.resetInlineComment();
	}

	// Does this register need to be restored to its original value before our function returns?
	static const bool RegisterNeedsRestoring(x86::Gp reg)
	{
		if (reg == x86::eax || reg == x86::ecx || reg == x86::edx)
		{
			return false;
		}

		return true;
	}

	std::vector<x86::Gp> pushed;
	std::vector<std::pair<x86::Gp, uint32_t>> registers;
	uint32_t next_cookie;
};

Register::~Register()
{
	if (allocator == nullptr)
		return;

	allocator->Release(*this);
}

x86::Gp Register::Get()
{
	return allocator->Get(*this);
}

struct JitContext;

enum struct JitProcResult : uint32_t
{
	// The proc finished. The return value is at the top of the stack (and should be the only value there)
	Success,
	
	// The proc is sleeping or something - the passed in JitContext is now invalid
	Yielded,
};

typedef JitProcResult (*JitProc)(JitContext* ctx);

struct JitContext
{
	static const size_t DefaultSlotAllocation = 16;

	JitContext()
		: stack(new Value[DefaultSlotAllocation])
		, stack_allocated(DefaultSlotAllocation)
		, stack_proc_base(nullptr)
		, stack_top(stack)
	{}

	~JitContext() noexcept
	{
		delete[] stack;
		stack = nullptr;
	}

	JitContext(const JitContext& src)
	{
		stack_allocated = src.stack_allocated;
		stack = new Value[stack_allocated];
		stack_top = &stack[src.stack_top - src.stack];
		stack_proc_base = &stack[src.stack_proc_base - src.stack];
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext& operator=(const JitContext& src)
	{
		delete[] stack;
		stack = new Value[stack_allocated];
		stack_top = &stack[src.stack_top - src.stack];
		stack_proc_base = &stack[src.stack_proc_base - src.stack];
		std::copy(src.stack, src.stack_top, stack);
	}

	JitContext(JitContext&& src) noexcept
		: stack(std::exchange(src.stack, nullptr))
		, stack_allocated(std::exchange(src.stack_allocated, 0))
		, stack_top(std::exchange(src.stack_top, nullptr))
		, stack_proc_base(std::exchange(src.stack_proc_base, nullptr))
	{}

	JitContext& operator=(JitContext&& src) noexcept
	{
		std::swap(stack, src.stack);
		std::swap(stack_allocated, src.stack_allocated);
		std::swap(stack_top, src.stack_top);
		std::swap(stack_proc_base, src.stack_proc_base);
		return *this;
	}

	// The number of slots in use
	size_t Count()
	{
		return stack_top - stack;
	}

	// Makes sure at least this many free slots are available
	// Called by JitProc prologues
	void Reserve(size_t slots)
	{
		size_t free_slots = stack_allocated - Count();

		if (free_slots >= slots)
			return;

		Value* old_stack = stack;
		Value* old_stack_top = stack_top;
		Value* old_stack_proc_base = stack_proc_base;

		stack_allocated += slots; // TODO: Growth factor?
		stack = new Value[stack_allocated];
		stack_top = &stack[old_stack_top - old_stack];
		stack_proc_base = &stack[old_stack_proc_base - old_stack];
		std::copy(old_stack, old_stack_top, stack);

		delete[] old_stack;
	}

	Value* stack;
	size_t stack_allocated;

	// Top of the stack. This is where the next stack entry to be pushed will be located.
	Value* stack_top;

	// Base of the current proc's stack
	Value* stack_proc_base;

	// Maybe?
	// ProcConstants* what_called_us;
	// Value dot;
};



struct ProcCompiler
{
	enum class InternalLabelRef : size_t
	{};

	struct InternalLabel
	{
		Label label;
	};

	enum class InternalRegisterRef : size_t
	{};

	struct InternalRegister
	{
		enum class Type
		{
			General,
			Xmm,

			JitContext, // Snowflake: always set to eax
			ESP, // Snowflake: always set to esp
		};

		static const uint32_t InvalidLoc = ~1;

		Type type;
		uint32_t startpoint;
		uint32_t endpoint;
	};

	// Either a constant literal or a reference to a virtual register
	struct Operand
	{
		enum class Type
		{
			Imm,
			Gp,
			Xmm,
			Label,
			MemGlobal,
			MemRegister,
			MemLabel,
		};

	protected:
		// Imm
		struct _Imm
		{
			uint32_t value;
		};

		// Gp, Xmm
		struct _Register
		{
			InternalRegisterRef ref;
		};

		// Label
		struct _Label
		{
			InternalLabelRef ref;
		};

		// Mem
		struct _MemGlobal
		{
			void* data;
			size_t size;
		};

		// MemRegister
		struct _MemRegister
		{
			InternalRegisterRef ref;
			ptrdiff_t offset;
			size_t size;
		};

		// MemLabel
		struct _MemLabel
		{
			InternalLabelRef ref;
			ptrdiff_t offset;
			size_t size;
		};

		//

		// Imm
		Operand(uint32_t value)
			: type(Type::Imm)
			, data(_Imm{value})
		{}

		// Gp / Xmm
		Operand(Type type, InternalRegisterRef ref)
			: type(type)
			, data(_Register{ref})
		{}

		// Label
		Operand(InternalLabelRef ref)
			: type(Type::Label)
			, data(_Label{ref})
		{}

		// MemGlobal
		Operand(void* data, size_t size)
			: type(Type::MemGlobal)
			, data(_MemGlobal{data, size})
		{}

		// MemRegister
		Operand(InternalRegisterRef ref, ptrdiff_t offset, size_t size)
			: type(Type::MemRegister)
			, data(_MemRegister{ref, offset, size})
		{}

		// MemLabel
		Operand(InternalLabelRef ref, ptrdiff_t offset, size_t size)
			: type(Type::MemLabel)
			, data(_MemLabel{ref, offset, size})
		{}

		Type type;

		std::variant<_Imm, _Register, _Label, _MemGlobal, _MemRegister, _MemLabel> data;

	public:
		Type GetType() { return type; }

		bool IsImm() { return type == Type::Imm; }
		bool IsGp() { return type == Type::Gp; }
		bool IsXmm() { return type == Type::Xmm; }
		bool IsLabel() { return type == Type::Label; }
		bool IsMemGlobal() { return type == Type::MemGlobal; }
		bool IsMemRegister() { return type == Type::MemRegister; }
		bool IsMemLabel() { return type == Type::MemLabel; }

		bool IsRegister() { return IsGp() || IsXmm(); }
		bool IsMem() { return IsMemGlobal() || IsMemRegister() || IsMemLabel(); }

		template<typename T>
		inline T& As() { return static_cast<T&>(*this); }
	};

	// A constant 32-bit value.
	struct Imm : Operand
	{
		Imm(uint32_t value)
			: Operand(value)
		{}

		uint32_t Value()
		{
			if (!IsImm())
			{
				throw;
			}

			return std::get<_Imm>(data).value;
		}
	};

	// A register
	struct Register : Operand
	{
	protected:
		Register(Type type, InternalRegisterRef ref)
			: Operand(type, ref)
		{}

	public:
		InternalRegisterRef Ref()
		{
			if (!IsRegister())
			{
				throw;
			}

			return std::get<_Register>(data).ref;
		}
	};

	// A general purpose virtual-register. Can be ecx, edx, etc.
	struct Gp : public Register
	{
		Gp() // eck
			: Register(Type::Gp, static_cast<InternalRegisterRef>(-1))
		{}


		Gp(InternalRegisterRef ref)
			: Register(Type::Gp, ref)
		{}
	};

	// An xmm virtual-register. Can be xmm0, xmm1, etc.
	struct Xmm : Register
	{
		Xmm(InternalRegisterRef ref)
			: Register(Type::Xmm, ref)
		{}
	};

	// A label
	struct Label : Operand
	{
		Label() // eck
			: Operand(static_cast<InternalLabelRef>(-1))
		{}

		Label(InternalLabelRef ref)
			: Operand(ref)
		{}

		InternalLabelRef Ref()
		{
			if (!IsLabel())
			{
				throw;
			}

			return std::get<_Label>(data).ref;
		}
	};

	struct Mem : Operand
	{
		protected:
			// MemGlobal
			Mem(void* data, size_t size)
				: Operand(data, size)
			{}

			// MemRegister
			Mem(InternalRegisterRef ref, ptrdiff_t offset, size_t size)
				: Operand(ref, offset, size)
			{}

			// MemLabel
			Mem(InternalLabelRef ref, ptrdiff_t offset, size_t size)
				: Operand(ref, offset, size)
			{}
	};

	// Memory reference to a global ptr
	struct MemGlobal : Mem
	{
		explicit MemGlobal(void* data, size_t size = sizeof(uint32_t))
			: Mem(data, size)
		{}

		void* Ptr()
		{
			if (!IsMemGlobal())
			{
				throw;
			}

			return std::get<_MemGlobal>(data).data;
		}

		size_t Size()
		{
			if (!IsMemGlobal())
			{
				throw;
			}

			return std::get<_MemGlobal>(data).size;
		}
	};

	// Memory reference to a register with an optional offset
	struct MemRegister : Mem
	{
		explicit MemRegister(Gp& reg, ptrdiff_t offset = 0, size_t size = sizeof(uint32_t))
			: Mem(reg.Ref(), offset, size)
		{}

		InternalRegisterRef Ref()
		{
			if (!IsMemRegister())
			{
				throw;
			}

			return std::get<_MemRegister>(data).ref;
		}

		ptrdiff_t Offset()
		{
			if (!IsMemRegister())
			{
				throw;
			}

			return std::get<_MemRegister>(data).offset;
		}

		size_t Size()
		{
			if (!IsMemRegister())
			{
				throw;
			}

			return std::get<_MemRegister>(data).size;
		}
	};

	// Memory reference to a label with an optional offset
	struct MemLabel : Mem
	{
		explicit MemLabel(Label& label, ptrdiff_t offset = 0, size_t size = sizeof(uint32_t))
			: Mem(label.Ref(), offset, size)
		{}

		InternalLabelRef Ref()
		{
			if (!IsMemLabel())
			{
				throw;
			}

			return std::get<_MemLabel>(data).ref;
		}

		ptrdiff_t Offset()
		{
			if (!IsMemLabel())
			{
				throw;
			}

			return std::get<_MemLabel>(data).offset;
		}

		size_t Size()
		{
			if (!IsMemLabel())
			{
				throw;
			}

			return std::get<_MemLabel>(data).size;
		}
	};

	// Helpers to make writing pointers friendly
	static MemGlobal Ptr(void* data, size_t size = sizeof(uint32_t))
	{
		return MemGlobal(data, size);
	}

	static MemRegister Ptr(Gp reg, ptrdiff_t offset = 0, size_t size = sizeof(uint32_t))
	{
		return MemRegister(reg, offset, size);
	}

	static MemLabel Ptr(Label label, ptrdiff_t offset = 0, size_t size = sizeof(uint32_t))
	{
		return MemLabel(label, offset, size);
	}

	struct Variable
	{
		Operand Type;
		Operand Value;
	};

	// these are a bit of a hack
	Gp jit_context;
	Gp ret;
	Gp esp;

	ProcCompiler(x86::Assembler& ass, uint32_t locals_count)
		: ass(&ass)
		, consts(&ass._code->_zone)
	{
		jit_context = AllocateJitContextPtr();
		ret = jit_context;
		esp = AllocateESP();
		consts_label = AllocateLabel();
		epilogue_label = AllocateLabel();
	}

	Gp AllocateJitContextPtr()
	{
		registers.push_back({InternalRegister::Type::JitContext, InternalRegister::InvalidLoc, InternalRegister::InvalidLoc});
		return {static_cast<InternalRegisterRef>( registers.size() - 1 )};
	}

	Gp AllocateESP()
	{
		registers.push_back({InternalRegister::Type::ESP, InternalRegister::InvalidLoc, InternalRegister::InvalidLoc});
		return {static_cast<InternalRegisterRef>( registers.size() - 1 )};
	}

	Gp AllocateUInt32()
	{
		registers.push_back({InternalRegister::Type::General, InternalRegister::InvalidLoc, InternalRegister::InvalidLoc});
		return {static_cast<InternalRegisterRef>( registers.size() - 1 )};
	}

	Xmm AllocateXmm()
	{
		registers.push_back({InternalRegister::Type::Xmm, InternalRegister::InvalidLoc, InternalRegister::InvalidLoc});
		return {static_cast<InternalRegisterRef>( registers.size() - 1 )};
	}

	Label AllocateLabel()
	{
		labels.push_back(ass->newLabel());
		return {static_cast<InternalLabelRef>( labels.size() - 1 )};
	}

	Mem AllocateConstantUInt32(uint32_t value)
	{
		size_t offset;
		if(consts.add(&value, sizeof(value), offset) != kErrorOk)
		{
			throw;
		}

		uint32_t test = 0x1A1A1A1A;
		consts.add(&test, sizeof(test), offset);

		return MemLabel(consts_label, offset, sizeof(value));
	}

	InternalRegister& GetInternalRegister(MemRegister& mem)
	{
		return registers[static_cast<size_t>(mem.Ref())];
	}

	InternalRegister& GetInternalRegister(Register& reg)
	{
		return registers[static_cast<size_t>(reg.Ref())];
	}

	void Bind(Label label)
	{
		label_bindings.emplace(instructions.size(), label.Ref());
	}

	void UseOperand(Operand op)
	{
		if (op.IsRegister())
		{
			InternalRegister& reg = GetInternalRegister(op.As<Register>());
			if (reg.startpoint == InternalRegister::InvalidLoc)
				reg.startpoint = instructions.size();
			reg.endpoint = instructions.size();
		}

		if (op.IsMemRegister())
		{
			InternalRegister& reg = GetInternalRegister(op.As<MemRegister>());
			if (reg.startpoint == InternalRegister::InvalidLoc)
				reg.startpoint = instructions.size();
			reg.endpoint = instructions.size();
		}
	}

	void emit_movss(Xmm op1, Xmm op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMovss, op1, op2);
	}

	void emit_movss(Mem op1, Xmm op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMovss, op1, op2);
	}

	void emit_movss(Xmm op1, Mem op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMovss, op1, op2);
	}

	void emit_addss(Operand op1, Operand op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdAddss, op1, op2);
	}

	void emit_mov(Gp op1, Gp op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMov, op1, op2);
	}

	void emit_mov(Gp op1, Imm op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMov, op1, op2);
	}

	void emit_mov(Mem op1, Imm op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMov, op1, op2);
	}

	void emit_mov(Mem op1, Gp op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMov, op1, op2);
	}

	void emit_mov(Gp op1, Mem op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMov, op1, op2);
	}


	void emit_movd(Xmm op1, Gp op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMovd, op1, op2);
	}

	void emit_movd(Gp op1, Xmm op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdMovd, op1, op2);
	}

	void emit_sub(Operand op1, Operand op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdSub, op1, op2);
	}

	void emit_sub(Mem op1, Operand op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdSub, op1, op2);
	}

	void emit_add(Operand op1, Mem op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdAdd, op1, op2);
	}

	void emit_add(Mem op1, Operand op2)
	{
		UseOperand(op1);
		UseOperand(op2);
		instructions.emplace_back(x86::Inst::Id::kIdAdd, op1, op2);
	}

	void SetLocal(size_t index, Variable& var)
	{
		locals[index] = var;
	}

	Variable GetLocal(size_t index)
	{
		auto& entry = locals[index];
		if (entry.has_value())
		{
			return *entry;
		}

		auto stack_proc_base = AllocateUInt32();
		emit_mov(stack_proc_base, Ptr(jit_context, offsetof(JitContext, stack_proc_base)));

		auto type = AllocateUInt32();
		auto value = AllocateUInt32();

		emit_mov(type, Ptr(stack_proc_base, sizeof(Value) * (index + 1)));
		emit_mov(value, Ptr(stack_proc_base, sizeof(Value) *  (index + 1)));

		Variable var{type, value};
		locals[index] = var; // TODO: This will cause the local to be set by CommitLocals even though we haven't changed it
		return var;
	}

	void PushStack(Variable entry)
	{
		stack.push_back(entry);
	}

	Variable PopStack()
	{
		// Our cached stack could be empty if something was already pushed before our block of code was entered
		if (!stack.empty())
		{
			auto entry = stack.back();
			stack.pop_back();

			if (entry.has_value())
			{
				return *entry;
			}
		}

		auto stack_top = AllocateUInt32();
		emit_mov(stack_top, Ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));

		auto type = AllocateUInt32();
		auto value = AllocateUInt32();
		emit_mov(type, Ptr(stack_top, -sizeof(Value)));
		emit_mov(value, Ptr(stack_top, -(sizeof(Value) / 2)));
		return {type, value};
	}


	void Add()
	{
		auto xmm0 = AllocateXmm();
		auto xmm1 = AllocateXmm();
		auto res = AllocateUInt32();

		{
			auto lhs = PopStack();

			if (lhs.Value.IsGp())
			{
				emit_movd(xmm0, lhs.Value.As<Gp>());
			}
			else
			{
				auto data = AllocateConstantUInt32(lhs.Value.As<Imm>().Value());
				emit_movss(xmm0, data);
			}
		}

		{
			auto rhs = PopStack();
			if (rhs.Value.IsGp())
			{
				emit_movd(xmm1, rhs.Value.As<Gp>());
			}
			else
			{
				auto data = AllocateConstantUInt32(rhs.Value.As<Imm>().Value());
				emit_movss(xmm1, data);
			}
		}

		emit_addss(xmm0, xmm1);
		emit_movd(res, xmm0);

		PushStack({Imm{DataType::NUMBER}, res});
	}

	// Does this register need to be restored to its original value before our function returns?
	static const bool RegisterNeedsRestoring(x86::Gp reg)
	{
		if (reg == x86::eax || reg == x86::ecx || reg == x86::edx)
		{
			return false;
		}

		return true;
	}

	struct RegisterAllocator
	{
		// Virtual-Register -> Physical-Register
		std::map<InternalRegisterRef, x86::Reg> Allocations;

		// Virtual-Registers that are currently stored in memory @ stack+offset
		// Only for Gp registers
		std::map<InternalRegisterRef, size_t> Spilled;

		// Physical-Registers that we need to restore to their origin value at the end of our block
		// In order from stack+0 -> stack+n
		std::vector<x86::Reg> Saved;

		// How much space our block requires on the stack
		size_t CalculateStackSize()
		{
			size_t size = Saved.size() * sizeof(uint32_t);

			// TODO: We only need to count 
			for (auto& [k, v] : Spilled)
			{
				size += sizeof(uint32_t);
			}

			return size;
		}

		void AllocateRegisters()
		{
			Allocations.clear();
			Spilled.clear();
			Saved.clear();

			// register, allocated, type
			using RegisterState = std::tuple<x86::Reg, bool, InternalRegister::Type>;
			std::array<RegisterState, 14> register_states{{
				{x86::ecx, false, InternalRegister::Type::General},
				{x86::edx, false, InternalRegister::Type::General},
				{x86::ebx, false, InternalRegister::Type::General},
				{x86::ebp, false, InternalRegister::Type::General},
				{x86::esi, false, InternalRegister::Type::General},
				{x86::edi, false, InternalRegister::Type::General},
				{x86::xmm0, false, InternalRegister::Type::Xmm},
				{x86::xmm1, false, InternalRegister::Type::Xmm},
				{x86::xmm2, false, InternalRegister::Type::Xmm},
				{x86::xmm3, false, InternalRegister::Type::Xmm},
				{x86::xmm4, false, InternalRegister::Type::Xmm},
				{x86::xmm5, false, InternalRegister::Type::Xmm},
				{x86::xmm6, false, InternalRegister::Type::Xmm},
				{x86::xmm7, false, InternalRegister::Type::Xmm}}};

			std::vector<InternalRegisterRef> active;
			/*
			// Sorted in the order that they are first used
			std::vector<InternalRegisterRef> register_init_order;
			{
				register_init_order.resize(registers.size());
				for (size_t i = 0; i < registers.size(); i++)
				{
					register_init_order[i] = static_cast<InternalRegisterRef>(i);
				}
				std::sort(register_init_order.begin(), register_init_order.end(),
					[this](const InternalRegisterRef& lhs, const InternalRegisterRef& rhs) { return registers[static_cast<size_t>(lhs)].startpoint < registers[static_cast<size_t>(rhs)].startpoint; });
			}

			for (auto i : register_init_order)
			{
				auto& i_reg = registers[static_cast<size_t>(i)];

				// Register was never referenced?
				if (i_reg.startpoint == InternalRegister::InvalidLoc)
					continue;

				// Early exit for our special cases
				switch (i_reg.type)
				{
					case InternalRegister::Type::ESP:
						Allocations.emplace(i, x86::esp);
						continue;
					case InternalRegister::Type::JitContext:
						Allocations.emplace(i, x86::eax);
						continue;
				}

				// Free any active registers no longer being used
				auto it = active.begin();
				while (it != active.end())
				{
					auto& active_reg = registers[static_cast<size_t>(*it)];
					if (active_reg.endpoint >= i_reg.startpoint)
						break;

					// Mark the register as available
					x86::Reg allocated_register = Allocations.at(*it);
					auto register_state = std::find_if(register_states.begin(), register_states.end(),
						[allocated_register](const std::tuple<x86::Reg, bool, InternalRegister::Type> v) { return std::get<0>(v)== allocated_register; });
					std::get<1>(*register_state) = false;

					// Remove us from active
					it = active.erase(it);
				}

				// Find a free register for this var
				auto allocated_register = std::find_if(register_states.begin(), register_states.end(),
					[&i_reg](const std::tuple<x86::Reg, bool, InternalRegister::Type> v) { return std::get<1>(v) == false && std::get<2>(v) == i_reg.type; });

				if (allocated_register == register_states.end())
				{
					if (i_reg.type == InternalRegister::Type::General)
					{
						Spilled.emplace(i);
					}

					// Ran out of registers. We'll need to implement spillover to the stack
					__debugbreak();
				}

				Allocations.emplace(i, std::get<0>(*allocated_register));
				std::get<1>(*allocated_register) = true;

				// Mark us as active, the vector has to remain sorted by ascending endpoint
				active.insert(std::upper_bound(active.begin(), active.end(), i_reg.endpoint,
					[this](const uint32_t& endpoint, const InternalRegisterRef& ref) { return endpoint < registers[static_cast<size_t>(ref)].endpoint; }), static_cast<InternalRegisterRef>(i));
			}
			*/
		}
	};



	asmjit::Operand ConvertOperand(std::map<InternalRegisterRef, x86::Reg>& allocated_registers, Operand& op)
	{
		switch (op.GetType())
		{
			case Operand::Type::Imm:
				return asmjit::Imm(op.As<Imm>().Value());
			case Operand::Type::Gp:
			case Operand::Type::Xmm:
				return allocated_registers[op.As<Register>().Ref()];
			case Operand::Type::Label:
				return labels[static_cast<size_t>(op.As<Label>().Ref())];
			case Operand::Type::MemGlobal:
			{
				MemGlobal& mem = op.As<MemGlobal>();
				return asmjit::x86::ptr(reinterpret_cast<uint32_t>( mem.Ptr() ), mem.Size());
			}
			case Operand::Type::MemRegister:
			{
				MemRegister& mem = op.As<MemRegister>();
				return asmjit::x86::ptr(allocated_registers[mem.Ref()].as<asmjit::x86::Gp>(), mem.Offset(), mem.Size());
			}
			case Operand::Type::MemLabel:
			{
				MemLabel& mem = op.As<MemLabel>();
				return asmjit::x86::ptr(labels[static_cast<size_t>(mem.Ref())], mem.Offset(), mem.Size());
			}
		}

		__debugbreak();
		return asmjit::Operand();
	}

	void Compile()
	{
		std::map<InternalRegisterRef, x86::Reg> allocated_registers; // = AllocateRegisters();

		for (size_t i = 0; i < instructions.size(); i++)
		{
			auto& tuple = instructions[i];
			uint32_t instruction = std::get<0>(tuple);
			Operand& op1 = std::get<1>(tuple);
			Operand& op2 = std::get<2>(tuple);

			auto range = label_bindings.equal_range(i);
			for (auto i = range.first; i != range.second; i++)
			{
				ass->bind(labels[static_cast<size_t>(i->second)]);
			}

			ass->emit(instruction, ConvertOperand(allocated_registers, op1), ConvertOperand(allocated_registers, op2));
		}

		// TODO: Make the current InternalRegisterRefs all fail if used after this
		registers.clear();
		instructions.clear();
		label_bindings.clear();

		// TODO: move me
		ass->embedConstPool(labels[static_cast<size_t>(consts_label.Ref())], consts);
	}

	void EmitPrologue()
	{
		// Get JitContext* from our received arguments
		emit_mov(jit_context, Ptr(esp, 4));

		// TODO: Allocate space in the stack if necessary

		// Reserve static space in the stack for our locals
		// stack_top = stack_top + (room the current stack_proc_base and our locals)
		auto new_stack_proc_base = AllocateUInt32();
		emit_mov(new_stack_proc_base, Ptr(jit_context, offsetof(JitContext, stack_top)));
		emit_add(Ptr(jit_context, offsetof(JitContext, stack_top)), Imm((1 +  locals.size()) * sizeof(Value)));

		// Store previous proc's stack_proc_base at the start of our stack
		// *new_stack_proc_base = {DataType::NULL, stack_proc_base}
		auto old_stack_proc_base = AllocateUInt32();
		emit_mov(old_stack_proc_base, Ptr(jit_context, offsetof(JitContext, stack_proc_base)));
		emit_mov(Ptr(new_stack_proc_base), Imm(DataType::NULL_D));
		emit_mov(Ptr(new_stack_proc_base, sizeof(Value) / 2), old_stack_proc_base);

		// Set new stack_proc_base
		emit_mov(Ptr(jit_context, offsetof(JitContext, stack_proc_base)), new_stack_proc_base);
	}

	void EmitEpilogue()
	{
		auto return_value = PopStack();

		// We don't need to commit any changed locals here because they're all about to die anyway
		// CommitLocals();

		// We don't need to commit the stack either because the whole thing's about to disappear (and we push our ret val manually at the end)
		// CommitStack();

		// stack_top = stack_proc_base
		auto stack_proc_base = AllocateUInt32();
		emit_mov(stack_proc_base, Ptr(jit_context, offsetof(JitContext, stack_proc_base)));
		emit_mov(Ptr(jit_context, offsetof(JitContext, stack_top)), stack_proc_base);

		// stack_proc_base = *stack_proc_base
		auto old_stack_proc_base = AllocateUInt32();
		emit_mov(old_stack_proc_base, Ptr(stack_proc_base, sizeof(Value) / 2));
		emit_mov(Ptr(jit_context, offsetof(JitContext, stack_proc_base)), old_stack_proc_base);

		// Push the return value on to the stack manually
		if (return_value.Type.IsGp())
		{
			emit_mov(Ptr(stack_proc_base), return_value.Type.As<Gp>());
		}
		else
		{
			emit_mov(Ptr(stack_proc_base), return_value.Type.As<Imm>());
		}

		if (return_value.Value.IsGp())
		{
			emit_mov(Ptr(stack_proc_base, sizeof(Value) / 2), return_value.Value.As<Gp>());
		}
		else
		{
			emit_mov(Ptr(stack_proc_base, sizeof(Value) / 2), return_value.Value.As<Imm>());
		}

		emit_add(Ptr(jit_context, offsetof(JitContext, stack_top)), Imm(sizeof(Value)));
		// emit_ret()
	}

	void CommitLocals()
	{
		auto stack_proc_base = AllocateUInt32();
		emit_mov(stack_proc_base, Ptr(jit_context, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)));

		for (size_t i = 0; i < locals.size(); i++)
		{
			auto& entry = locals[i];

			// We haven't changed this one
			if (!entry.has_value())
				continue;

			if (entry->Type.IsGp())
			{
				emit_mov(Ptr(stack_proc_base, (1 + i) * sizeof(Value)), entry->Type.As<Gp>());
			}
			else
			{
				emit_mov(Ptr(stack_proc_base, (1 + i) * sizeof(Value)), entry->Type.As<Imm>());
			}

			if (entry->Value.IsGp())
			{
				emit_mov(Ptr(stack_proc_base, (1 + i) * sizeof(Value) + sizeof(Value) / 2), entry->Value.As<Gp>());
			}
			else
			{
				emit_mov(Ptr(stack_proc_base, (1 + i) * sizeof(Value) + sizeof(Value) / 2), entry->Value.As<Imm>());
			}
		}
	}

	void CommitStack()
	{
		auto stack_top = AllocateUInt32();
		emit_mov(stack_top, Ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));

		for (size_t i = 0; i < stack.size(); i++)
		{
			auto& entry = stack[i];
			size_t offset = stack.size() - i;

			// We haven't changed this one
			if (!entry.has_value())
				continue;

			if (entry->Type.IsGp())
			{
				emit_mov(Ptr(stack_top, -offset * sizeof(Value)), entry->Type.As<Gp>());
			}
			else
			{
				emit_mov(Ptr(stack_top, -offset * sizeof(Value)), entry->Type.As<Imm>());
			}

			if (entry->Value.IsGp())
			{
				emit_mov(Ptr(stack_top, -offset * sizeof(Value) + sizeof(Value) / 2), entry->Value.As<Gp>());
			}
			else
			{
				emit_mov(Ptr(stack_top, -offset * sizeof(Value) + sizeof(Value) / 2), entry->Value.As<Imm>());
			}
		}

		// Forget about everything
		// TODO: Might belong in StartBlock really
		stack.clear();
	}

	void BeginProc(uint32_t locals_count)
	{
		locals.resize(locals_count);
		for (uint32_t i = 0; i < locals_count; i++)
		{
			SetLocal(0, ProcCompiler::Variable{Imm(DataType::NULL_D), Imm(0)});
		}
	}

	void FinishProc()
	{
		Bind(epilogue_label);
		EmitEpilogue();
		// emit_ret()
	}

	void BeginBlock()
	{

	}

	void FinishBlock()
	{
		// We have to compile at the end of every block
		Compile();

		// We have to forget about any registers/imms we had for stack values & locals
		stack.clear();

		for (auto& entry : locals)
		{
			entry.reset();
		}

		CommitLocals(); // TODO: don't need to do this for the last block?
	}

	void Return()
	{
		// emit_jmp(epilogue_label);
	}

	void Yield(Label& resume)
	{
		uint32_t resumption_index = 1337; // TODO:

		// We'll need this later
		PushStack({Imm(DataType::NULL_D), Imm(resumption_index)});

		CommitLocals();
		CommitStack();

		// emit_call(jit_co_suspend_internal)
		emit_mov(ret, Imm(static_cast<uint32_t>(JitProcResult::Yielded)));
		// emit_ret()

		FinishBlock();

		// Our new entry-point
		Bind(resume);
		emit_mov(jit_context, Ptr(esp, 4));
	}

private:

	Label epilogue_label;
	Label consts_label;
	ConstPool consts;

	std::vector<std::tuple<uint32_t, Operand, Operand>> instructions;

	x86::Assembler* ass;

	std::multimap<size_t, InternalLabelRef> label_bindings;

	std::vector<asmjit::Label> labels;
	std::vector<InternalRegister> registers;

	std::vector<std::optional<Variable>> locals;
	std::vector<std::optional<Variable>> stack;
};






static RegisterAllocator register_allocator;
static unsigned int jit_co_suspend_proc_id = 0;

// Shouldn't be globals!!!
static size_t locals_count = 0;
static std::map<unsigned int, Label> proc_labels;
static std::map<unsigned int, Label> proc_epilogues;
static std::set<std::string> string_set;

static std::vector<Label> resumption_labels;
static std::vector<JitProc> resumption_procs;

static Label current_epilogue;

static uint32_t EncodeFloat(float f)
{
	return *reinterpret_cast<uint32_t*>(&f);
}

static float DecodeFloat(uint32_t i)
{
	return *reinterpret_cast<float*>(&i);
}

std::map<unsigned int, Block> split_into_blocks(Disassembly& dis, x86::Assembler& ass, size_t& locals_max_count)
{
	std::map<unsigned int, Block> blocks;
	unsigned int current_block_offset = 0;
	blocks[current_block_offset] = Block(0);
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
			blocks[current_block_offset] = Block(current_block_offset);
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
				Block& victim = (--blocks.lower_bound(target))->second; //get the block that contains the target offset
				auto split_point = std::find_if(victim.contents.begin(), victim.contents.end(), [target](Instruction& instr) { return instr.offset() == target; }); //find the target instruction
				Block new_block = Block(target);
				new_block.contents = std::vector<Instruction>(split_point, victim.contents.end());
				victim.contents.erase(split_point, victim.contents.end()); //split
				blocks[target] = new_block;
			}
			current_block_offset = i.offset()+i.size();
			blocks[current_block_offset] = Block(current_block_offset);
		}
	}
	static std::ofstream o("blocks.txt");
	o << "BEGIN: " << dis.proc->name << '\n';
	for (auto& [offset, block] : blocks)
	{
		block.label2 = ass.newLabel();
		o << offset << ":\n";
		for (const Instruction& i : block.contents)
		{
			o << i.opcode().tostring() << "\n";
		}
		o << "\n";
	}
	o << '\n';

	return blocks;
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

// obviously not final, just some global state to test jit pause/resume
// reentrancy will kill it, don't complain
static Register jit_context = { nullptr, 0, 0 };
static JitContext stored_context;
static JitContext* current_context = nullptr;

static trvh JitEntryPoint(void* code_base, unsigned int args_len, Value* args, Value src)
{
	JitContext ctx;
	JitProc code = static_cast<JitProc>(code_base);

	current_context = &ctx;
	JitProcResult res = code(&ctx);
	current_context = nullptr;

	switch (res)
	{
	case JitProcResult::Success:
		if (ctx.Count() != 1)
		{
			__debugbreak();
			return Value::Null();
		}

		return ctx.stack[0];
	case JitProcResult::Yielded:
		return Value::Null();
	}

	// Shouldn't be here
	__debugbreak();
	return Value::Null();
}

static void jit_co_suspend_internal()
{
	stored_context = std::move(*current_context);
}

static trvh jit_co_suspend(unsigned int argcount, Value* args, Value src)
{
	// This should never run, it's a compiler intrinsic
	__debugbreak();
	return Value::Null();
}

static trvh jit_co_resume(unsigned int argcount, Value* args, Value src)
{
	Value resume_data = *(stored_context.stack_top - 1);

	// Now we push the value to be returned by jit_co_suspend
	if (argcount > 0)
	{
		*(stored_context.stack_top - 1) = args[0];
	}
	else
	{
		*(stored_context.stack_top - 1) = Value::Null();
	}

	JitProcResult res = resumption_procs[resume_data.value](&stored_context);

	switch (res)
	{
		case JitProcResult::Success:
			if (stored_context.Count() != 1)
			{
				__debugbreak();
				return Value::Null();
			}

			return stored_context.stack[0];
		case JitProcResult::Yielded:
			return Value::Null();
	}
	
	// Shouldn't be here
	__debugbreak();
	return Value::Null();
}


static void Emit_PushInteger(x86::Assembler& ass, float not_an_integer)
{
	auto stack_top = register_allocator.New(ass);

	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top)));
	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top)), sizeof(Value));

	ass.mov(x86::ptr(stack_top, 0), Imm(DataType::NUMBER));
	ass.mov(x86::ptr(stack_top, sizeof(Value) / 2), Imm(EncodeFloat(not_an_integer)));
}

static void Emit_SetLocal(x86::Assembler& ass, unsigned int id)
{
	ass.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

	auto stack_top = register_allocator.New(ass);
	auto stack_proc_base = register_allocator.New(ass);
	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top)));
	ass.mov(stack_proc_base, x86::ptr(jit_context, offsetof(JitContext, stack_proc_base)));

	auto temp = register_allocator.New(ass);
	ass.mov(temp, x86::ptr(stack_top, 0));
	ass.mov(x86::ptr(stack_proc_base, id * sizeof(Value)), temp);

	ass.mov(temp, x86::ptr(stack_top, sizeof(Value) / 2));
	ass.mov(x86::ptr(stack_proc_base, id * sizeof(Value) + sizeof(Value) / 2), temp);
}

static void Emit_GetLocal(x86::Assembler& ass, unsigned int id)
{
	auto stack_top = register_allocator.New(ass);
	auto stack_proc_base = register_allocator.New(ass);

	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top)));
	ass.mov(stack_proc_base, x86::ptr(jit_context, offsetof(JitContext, stack_proc_base)));

	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top)), sizeof(Value));

	// Register to copy the type/value with
	auto temp = register_allocator.New(ass);

	ass.mov(temp, x86::ptr(stack_proc_base, id * sizeof(Value)));
	ass.mov(x86::ptr(stack_top, 0, sizeof(uint32_t)), temp);

	ass.mov(temp, x86::ptr(stack_proc_base, id * sizeof(Value) + sizeof(Value) / 2));
	ass.mov(x86::ptr(stack_top, sizeof(Value) / 2), temp);
}

static void Emit_Pop(x86::Assembler& ass)
{
	ass.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top)), sizeof(Value));
}

static void Emit_PushValue(x86::Assembler& ass, DataType type, unsigned int value, unsigned int value2 = 0)
{
	if (type == DataType::NUMBER)
	{
		value = value << 16 | value2;
	}

	auto stack_top = register_allocator.New(ass);
	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top)));
	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top)), sizeof(Value));

	ass.mov(x86::ptr(stack_top, 0), Imm(type));
	ass.mov(x86::ptr(stack_top, sizeof(Value) / 2), Imm(value));
}

static void Emit_CallGlobal(x86::Assembler& ass, unsigned int arg_count, unsigned int proc_id)
{
	if (proc_id == jit_co_suspend_proc_id)
	{
		ass.setInlineComment("jit_co_suspend intrinsic");

		Label resume = ass.newLabel();

		resumption_labels.push_back(resume);
		uint32_t resumption_index = resumption_labels.size() - 1;

		auto stack_top = register_allocator.New(ass);

		ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));
		ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));

		// where the code should resume
		ass.mov(x86::ptr(stack_top, 0, sizeof(uint32_t)), Imm(DataType::NULL_D));
		ass.mov(x86::ptr(stack_top, sizeof(Value) / 2, sizeof(uint32_t)), resumption_index);

		// Our JitContext* becomes invalid after this call
		ass.call((uint32_t) jit_co_suspend_internal);
		ass.mov(x86::eax, Imm(static_cast<uint32_t>(JitProcResult::Yielded)));
		register_allocator.Flush(ass);
		ass.ret();

		std::string comment = "Resumption Label: " + std::to_string(resumption_index);
		auto it = string_set.insert(comment);
		ass.setInlineComment(it.first->c_str());

		// Emit a new entry point for our proc
		ass.bind(resume);
		jit_context = register_allocator.New(ass);
		ass.mov(jit_context, x86::ptr(x86::esp, 4, sizeof(uint32_t)));
		return;
	}

	ass.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), arg_count * sizeof(Value));

	// TODO: Currently no register allocator allowed while we mess with the stack :/
	register_allocator.Flush(ass);
	{
		ass.mov(x86::eax, x86::ptr(x86::esp, 4, sizeof(uint32_t)));
		ass.mov(x86::ecx, x86::ptr(x86::eax, offsetof(JitContext, stack_top), sizeof(uint32_t)));

		ass.sub(x86::esp, 4 * 12);

		ass.mov(x86::ptr(x86::esp, 4 * 0, sizeof(uint32_t)), 0); // usr_type
		ass.mov(x86::ptr(x86::esp, 4 * 1, sizeof(uint32_t)), 0); // usr_value
		ass.mov(x86::ptr(x86::esp, 4 * 2, sizeof(uint32_t)), 2); // proc_type
		ass.mov(x86::ptr(x86::esp, 4 * 3, sizeof(uint32_t)), proc_id); // proc_id
		ass.mov(x86::ptr(x86::esp, 4 * 4, sizeof(uint32_t)), 0); // const_0
		ass.mov(x86::ptr(x86::esp, 4 * 5, sizeof(uint32_t)), 0); // src_type
		ass.mov(x86::ptr(x86::esp, 4 * 6, sizeof(uint32_t)), 0); // src_value
		ass.mov(x86::ptr(x86::esp, 4 * 7, sizeof(uint32_t)), x86::ecx); // argList
		ass.mov(x86::ptr(x86::esp, 4 * 8, sizeof(uint32_t)), arg_count); // argListLen
		ass.mov(x86::ptr(x86::esp, 4 * 9, sizeof(uint32_t)), 0); // const_0_2
		ass.mov(x86::ptr(x86::esp, 4 * 10, sizeof(uint32_t)), 0); // const_0_3

		// Save stack_top on the stack
		ass.mov(x86::ptr(x86::esp, 4 * 11, sizeof(uint32_t)), x86::ecx);

		ass.call((uint32_t) CallGlobalProc);

		// Get stack_top back
		ass.mov(x86::ecx, x86::ptr(x86::esp, 4 * 11, sizeof(uint32_t)));

		// Put ret val into the stack
		ass.mov(x86::ptr(x86::ecx, 0, sizeof(uint32_t)), x86::eax);
		ass.mov(x86::ptr(x86::ecx, sizeof(Value) / 2, sizeof(uint32_t)), x86::edx);

		ass.add(x86::esp, 4 * 12);
	}

	// restore our jit_context
	jit_context = register_allocator.New(ass);
	ass.mov(jit_context, x86::ptr(x86::esp, 4, sizeof(uint32_t)));
	
	// Could merge into the sub above
	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value));
}

static void Emit_MathOp(x86::Assembler& ass, Bytecode op_type)
{
	ass.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), 2 * sizeof(Value)); //pop 2 values from stack

	auto stack_top = register_allocator.New(ass);

	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t))); //move stack top into ecx
	ass.movss(x86::xmm0, x86::ptr(stack_top, offsetof(Value, valuef), sizeof(uint32_t))); //move the float val of first value into xmm0
	switch (op_type)
	{
	case Bytecode::ADD:
		ass.addss(x86::xmm0, x86::ptr(stack_top, sizeof(Value) + offsetof(Value, valuef), sizeof(uint32_t))); //do meth
		break;
	case Bytecode::SUB:
		ass.subss(x86::xmm0, x86::ptr(stack_top, sizeof(Value) + offsetof(Value, valuef), sizeof(uint32_t)));
		break;
	case Bytecode::MUL:
		ass.mulss(x86::xmm0, x86::ptr(stack_top, sizeof(Value) + offsetof(Value, valuef), sizeof(uint32_t)));
		break;
	case Bytecode::DIV:
		ass.divss(x86::xmm0, x86::ptr(stack_top, sizeof(Value) + offsetof(Value, valuef), sizeof(uint32_t)));
		break;
	}
	ass.movss(x86::ptr(stack_top, offsetof(Value, valuef), sizeof(uint32_t)), x86::xmm0); //move result into existing stack value
	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), sizeof(Value)); //increment stack size
}

static void Emit_Return(x86::Assembler& ass)
{
	ass.jmp(current_epilogue);
}

static bool EmitBlock(x86::Assembler& ass, Block& block)
{
	ass.bind(block.label2);
	for (size_t i=0; i < block.contents.size(); i++)
	{
		Instruction& instr = block.contents[i];

		auto it = string_set.insert(instr.opcode().tostring());
		ass.setInlineComment(it.first->c_str());

		switch (instr.bytes()[0])
		{
		case Bytecode::PUSHI:
			jit_out << "Assembling push integer" << std::endl;
			Emit_PushInteger(ass, instr.bytes()[1]);
			break;
		case Bytecode::ADD:
		case Bytecode::SUB:
		case Bytecode::MUL:
		case Bytecode::DIV:
			jit_out << "Assembling math op" << std::endl;
			Emit_MathOp(ass, (Bytecode)instr.bytes()[0]);
			break;
		case Bytecode::SETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling set local" << std::endl;
				Emit_SetLocal(ass, instr.bytes()[2]);
			}
			break;
		case Bytecode::GETVAR:
			if (instr.bytes()[1] == AccessModifier::LOCAL)
			{
				jit_out << "Assembling get local" << std::endl;
				Emit_GetLocal(ass, instr.bytes()[2]);
			}
			break;
		case Bytecode::POP:
			jit_out << "Assembling pop" << std::endl;
			Emit_Pop(ass);
			break;
		case Bytecode::PUSHVAL:
			jit_out << "Assembling push value" << std::endl;
			if (instr.bytes()[1] == DataType::NUMBER) //numbers take up two DWORDs instead of one
			{
				Emit_PushValue(ass, (DataType)instr.bytes()[1], instr.bytes()[2], instr.bytes()[3]);
			}
			else
			{
				Emit_PushValue(ass, (DataType)instr.bytes()[1], instr.bytes()[2]);
			}
			break;
		case Bytecode::CALLGLOB:
			jit_out << "Assembling call global" << std::endl;
			Emit_CallGlobal(ass, instr.bytes()[1], instr.bytes()[2]);
			break;
		case Bytecode::RET:
			Emit_Return(ass);
			break;
		case Bytecode::DBG_FILE:
		case Bytecode::DBG_LINENO:
			break;
		default:
			jit_out << "Unknown instruction: " << instr.opcode().tostring() << std::endl;
			break;
		}
	}

	return true;
}

static void ReserveStack(JitContext* ctx, uint32_t slots)
{
	ctx->Reserve(slots);
}

static void EmitPrologue(x86::Assembler& ass)
{
	jit_context = register_allocator.New(ass);
	ass.mov(jit_context, x86::ptr(x86::esp, 4, sizeof(uint32_t)));

	// TODO: Call JitContext::Reserve()

	// TODO: We need to just define a struct of the stuff that goes before locals somewhere
	// Allocate room for our constant data. It's the previous stack_proc_base and our local vars
	auto old_stack_top = register_allocator.New(ass);

	ass.mov(old_stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));
	ass.add(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), (1 + locals_count) * sizeof(Value));

	// Store previous proc's stack_proc_base
	{
		auto stack_proc_base = register_allocator.New(ass);
		ass.mov(stack_proc_base, x86::ptr(jit_context, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)));
		ass.mov(x86::ptr(old_stack_top, 0, sizeof(uint32_t)), Imm(0));
		ass.mov(x86::ptr(old_stack_top, sizeof(Value) / 2, sizeof(uint32_t)), stack_proc_base);
	}

	// Set new stack_proc_base
	ass.mov(x86::ptr(jit_context, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)), old_stack_top);

	// Set the locals to null
	for (size_t i = 0; i < locals_count; i++)
	{
		ass.mov(x86::ptr(old_stack_top, (1 + i) * sizeof(Value), sizeof(uint32_t)), Imm(0));
		ass.mov(x86::ptr(old_stack_top, (1 + i) * sizeof(Value) + sizeof(Value) / 2, sizeof(uint32_t)), Imm(0));
	}
}

static void EmitEpilogue(x86::Assembler& ass)
{
	auto stack_top = register_allocator.New(ass);
	ass.mov(stack_top, x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)));

	// Remove stack entries for our return value, the previous stack_proc_base, and all locals (except 1)
	ass.sub(x86::ptr(jit_context, offsetof(JitContext, stack_top), sizeof(uint32_t)), (1 + locals_count) * sizeof(Value));

	// Restore the old stack_proc_base
	{
		auto old_stack_proc_base = register_allocator.New(ass);
		ass.mov(old_stack_proc_base, x86::ptr(stack_top, sizeof(Value) * -(1 + locals_count) - (sizeof(Value) / 2), sizeof(uint32_t)));
		ass.mov(x86::ptr(jit_context, offsetof(JitContext, stack_proc_base), sizeof(uint32_t)), old_stack_proc_base);
	}

	// Move our return value from its old stack position to its new one
	{
		auto type = register_allocator.New(ass);
		ass.mov(type, x86::ptr(stack_top, -sizeof(Value), sizeof(uint32_t)));
		ass.mov(x86::ptr(stack_top, sizeof(Value) * -(1 + locals_count) - sizeof(Value), sizeof(uint32_t)), type);
	}
	{
		auto value = register_allocator.New(ass);
		ass.mov(value, x86::ptr(stack_top, -(sizeof(Value) / 2), sizeof(uint32_t)));
		ass.mov(x86::ptr(stack_top, sizeof(Value) * -(1 + locals_count) - (sizeof(Value) / 2), sizeof(uint32_t)), value);
	}

	register_allocator.Flush(ass);
	ass.mov(x86::eax, Imm(static_cast<uint32_t>(JitProcResult::Success)));
	ass.ret();
}

JitRuntime rt;
void jit_compile(std::vector<Core::Proc*> procs)
{
	FILE* fuck = fopen("asm.txt", "w");
	FileLogger logger(fuck);
	SimpleErrorHandler eh;
	CodeHolder code;
	code.init(rt.codeInfo());
	code.setLogger(&logger);
	code.setErrorHandler(&eh);
	x86::Assembler ass(&code);
	std::ofstream asd("raw.txt");

	ProcCompiler compiler(ass, 0);

	auto lab = compiler.AllocateLabel();
	compiler.Bind(lab);
	compiler.EmitPrologue();
	compiler.PushStack({ProcCompiler::Imm(DataType::NUMBER), ProcCompiler::Imm(EncodeFloat(12))});
	compiler.PushStack({ProcCompiler::Imm(DataType::NUMBER), ProcCompiler::Imm(EncodeFloat(8))});
	compiler.Add();
	compiler.EmitEpilogue();

	compiler.Compile();

	int eerr = ass.finalize();
	if (eerr)
	{
		jit_out << "Failed to assemble" << std::endl;
		return;
	}

	char* ccode_base = nullptr;
	eerr = rt.add(&ccode_base, &code);
	if (eerr)
	{
		jit_out << "Failed to add to runtime: " << eerr << std::endl;
		return;
	}

	return;



	resumption_labels.clear();
	resumption_procs.clear();

	for (auto& proc : procs)
	{
		proc_labels[proc->id] = ass.newLabel();
		proc_epilogues[proc->id] = ass.newLabel();
	}

	for (auto& proc : procs)
	{
		Disassembly dis = proc->disassemble();
		asd << "BEGIN " << proc->name << '\n';
		for (Instruction& i : dis)
		{
			asd << i.bytes_str() << std::endl;
		}
		asd << "END " << proc->name << '\n';

		current_epilogue = proc_epilogues[proc->id];

		// needed?
		// last_commit_size = 0;
		// stack_cache.clear();

		// TODO: locals_count here is ass
		auto blocks = split_into_blocks(dis, ass, locals_count);

		// Prologue
		std::string comment = "Proc Prologue: " + proc->raw_path;
		auto it = string_set.insert(comment);
		ass.setInlineComment(it.first->c_str());
		ass.bind(proc_labels[proc->id]);
		EmitPrologue(ass);

		for (auto& [k, v] : blocks)
		{
			std::string comment = "Proc Block: " + proc->raw_path + "+" + std::to_string(v.offset);
			auto it = string_set.insert(comment);
			ass.setInlineComment(it.first->c_str());
			//ass.bind(proc_labels[proc->id]);
			EmitBlock(ass, v);
		}

		// Epilogue
		std::string comment2 = "Proc Epilogue: " + proc->raw_path;
		auto it2 = string_set.insert(comment2);
		ass.setInlineComment(it2.first->c_str());
		ass.bind(proc_epilogues[proc->id]);
		EmitEpilogue(ass);
	}

	jit_out << "Finalizing\n";
	int err = ass.finalize();
	if (err)
	{
		jit_out << "Failed to assemble" << std::endl;
		return;
	}

	char* code_base = nullptr;
	err = rt.add(&code_base, &code);
	if (err)
	{
		jit_out << "Failed to add to runtime: " << err << std::endl;
		return;
	}

	// Setup resumption procs
	resumption_procs.resize(resumption_labels.size());
	for (size_t i = 0; i < resumption_labels.size(); i++)
	{
		resumption_procs[i] = reinterpret_cast<JitProc>(code_base + code.labelOffset(resumption_labels[i]));
	}

	// Hook procs
	for (auto& proc : procs)
	{
		Label& entry = proc_labels[proc->id];
		char* func_base = reinterpret_cast<char*>(code_base + code.labelOffset(entry));
		jit_out << func_base << std::endl;
		proc->jit_hook(func_base, JitEntryPoint);
	}

	jit_out << "Compilation successful" << std::endl;	
}

extern "C" EXPORT const char* jit_initialize(int n_args, const char** args)
{
	if (!Core::initialize())
	{
		return Core::FAIL;
	}
	
	Core::get_proc("/proc/jit_co_suspend").hook(jit_co_suspend);
	Core::get_proc("/proc/jit_co_resume").hook(jit_co_resume);

	jit_co_suspend_proc_id = Core::get_proc("/proc/jit_co_suspend").id;

	jit_compile({&Core::get_proc("/proc/jit_test_compiled_proc")});
	return Core::SUCCESS;
}
