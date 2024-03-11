#include "vkutil.hpp"

#include <vector>

#include <cstdio>
#include <cassert>

#include "error.hpp"
#include "to_string.hpp"

namespace labutils
{
	ShaderModule load_shader_module( VulkanContext const& aContext, char const* aSpirvPath )
	{
		//throw Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 2) implement me!
		assert(aSpirvPath);
		if (std::FILE* fin = std::fopen(aSpirvPath, "rb"))
		{
			std::fseek(fin, 0, SEEK_END);
			auto const bytes = std::size_t(std::ftell(fin));
			std::fseek(fin, 0, SEEK_SET);
			// SPIR-V consists of a number of 32-bit = 4 byte words
			assert(0 == bytes % 4);
			auto const words = bytes / 4;
			std::vector<std::uint32_t> code(words);
			std::size_t offset = 0;
			while (offset != words)
			{
				auto const read = std::fread(code.data() + offset, sizeof(std::uint32_t), words - offset, fin);
				if (0 == read)
				{
					std::fclose(fin);
					throw Error("Error reading %s: ferror = %d, feof = %d", aSpirvPath, std::ferror(fin), std::feof(fin));
				}
				offset += read;
			}
			std::fclose(fin);
			VkShaderModuleCreateInfo moduleInfo{};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = bytes;
			moduleInfo.pCode = code.data();
			VkShaderModule smod = VK_NULL_HANDLE;
			if (auto const res = vkCreateShaderModule(aContext.device, &moduleInfo, nullptr, &smod); VK_SUCCESS != res)
			{
				throw Error("Unable to create shader module from %s\n"
					"vkCreateShaderModule() returned %s", aSpirvPath, to_string(res).c_str());
			}
			return ShaderModule(aContext.device, smod);
		}
		throw Error("Cannont open %s for reading", aSpirvPath);
	}


	CommandPool create_command_pool( VulkanContext const& aContext, VkCommandPoolCreateFlags aFlags )
	{
		//throw Error( "Not yet implemented" ); //TODO: implement me!
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = aContext.graphicsFamilyIndex;
		poolInfo.flags = aFlags;
		VkCommandPool cpool = VK_NULL_HANDLE;
		if (auto const res = vkCreateCommandPool(aContext.device, &poolInfo, nullptr, &cpool); VK_SUCCESS != res)
		{
			throw Error("Unable to create command pool\n"
				"vkCreateCommandPool() returned %s", to_string(res).c_str());
		}
		return CommandPool(aContext.device, cpool);
	}

	VkCommandBuffer alloc_command_buffer( VulkanContext const& aContext, VkCommandPool aCmdPool )
	{
		//throw Error( "Not yet implemented" ); //TODO: implement me!
		VkCommandBufferAllocateInfo cbufInfo{};
		cbufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbufInfo.commandPool = aCmdPool;
		cbufInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbufInfo.commandBufferCount = 1;
		VkCommandBuffer cbuff = VK_NULL_HANDLE;
		if (auto const res = vkAllocateCommandBuffers(aContext.device, &cbufInfo, &cbuff); VK_SUCCESS != res)
		{
			throw Error("Unable to allocate command buffer\n"
				"vkAllocateCommandBuffers() returned %s", to_string(res).c_str());
		}
		return cbuff;
	}


	Fence create_fence( VulkanContext const& aContext, VkFenceCreateFlags aFlags )
	{
		//throw Error( "Not yet implemented" ); //TODO: implement me!
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = aFlags;
		VkFence fence = VK_NULL_HANDLE;
		if (auto const res = vkCreateFence(aContext.device, &fenceInfo, nullptr, &fence); VK_SUCCESS != res)
		{
			throw Error("Unable to create fence\n"
				"vkCreateFence() returned %s", to_string(res).c_str());
		}
		return Fence(aContext.device, fence);
	}

	Semaphore create_semaphore( VulkanContext const& aContext )
	{
		//throw Error( "Not yet implemented" ); //TODO: implement me!
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkSemaphore semaphore = VK_NULL_HANDLE;
		if (auto const res = vkCreateSemaphore(aContext.device, &semaphoreInfo, nullptr, &semaphore); VK_SUCCESS != res)
		{
			throw Error("Unable to create semaphore\n"
				"vkCreateSemaphore() returned %s", to_string(res).c_str());
		}
		return Semaphore(aContext.device, semaphore);
	}




}
