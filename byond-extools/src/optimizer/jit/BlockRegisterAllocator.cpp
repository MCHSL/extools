#include "BlockRegisterAllocator.h"
#include "JitContext.h"

namespace jit
{

using namespace asmjit;

Error BlockRegisterAllocator::run(Zone* zone, Logger* logger)
{
	BaseNode* node = cb()->firstNode();
	if (!node)
	{
		return kErrorOk;
	}

	do
	{
		if (node->type() == static_cast<uint32_t>(NodeTypes::kNodeBlock))
		{
			BlockNode* block = node->as<BlockNode>();
			node = block->_end;
			visitBlock(block, zone, logger);
		}

		node = node->next();
	} while (node);
}

void BlockRegisterAllocator::visitBlock(BlockNode* block, Zone* zone, Logger* logger)
{
	BaseNode* node = block->next();

	do
	{
		if (node->isInst())
		{
			InstNode* inst = node->as<InstNode>();
			
		}

		node = node->next();
	} while(node);
}

}