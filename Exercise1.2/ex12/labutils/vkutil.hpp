#pragma once

#include <volk/volk.h>

#include "vkobject.hpp"
#include "vulkan_context.hpp"



namespace labutils
{
	ShaderModule load_shader_module( VulkanContext const&, char const* aSpirvPath );

	CommandPool create_command_pool( VulkanContext const&, VkCommandPoolCreateFlags = 0 );
	VkCommandBuffer alloc_command_buffer( VulkanContext const&, VkCommandPool );

	Fence create_fence( VulkanContext const&, VkFenceCreateFlags = 0 );
	Semaphore create_semaphore( VulkanContext const& );



}
