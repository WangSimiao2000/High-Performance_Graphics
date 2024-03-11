#include <volk/volk.h>


#include <tuple>
#include <limits>
#include <vector>
#include <stdexcept>

#include <cstdio>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_window.hpp"
namespace lut = labutils;


namespace
{
	namespace cfg
	{
		// Compiled shader code for the graphics pipeline
		// See sources in exercise3/shaders/*. 
#		define SHADERDIR_ "assets/exercise3/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "triangle.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "triangle.frag.spv";
#		undef SHADERDIR_
	}

	// GLFW callbacks
	void glfw_callback_key_press( GLFWwindow*, int, int, int, int );

	// Helpers:
	lut::RenderPass create_render_pass( lut::VulkanWindow const& );

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& );
	lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout ); // VulkanWindow or VulkanContext?

	void create_swapchain_framebuffers( 
		lut::VulkanWindow const&, 
		VkRenderPass,
		std::vector<lut::Framebuffer>&
	);

	void record_commands( 
		VkCommandBuffer,
		VkRenderPass,
		VkFramebuffer,
		VkPipeline,
		VkExtent2D const&
	);
	void submit_commands(
		lut::VulkanContext const&,
		VkCommandBuffer,
		VkFence,
		VkSemaphore,
		VkSemaphore
	);
}

int main() try
{
	// Create our Vulkan Window
	lut::VulkanWindow window = lut::make_vulkan_window();

	// Configure the GLFW window
	glfwSetKeyCallback( window.window, &glfw_callback_key_press );

	// Intialize resources
	lut::RenderPass renderPass = create_render_pass( window );

	lut::PipelineLayout pipeLayout = create_triangle_pipeline_layout( window );
	lut::Pipeline pipe = create_triangle_pipeline( window, renderPass.handle, pipeLayout.handle );

	std::vector<lut::Framebuffer> framebuffers;
	create_swapchain_framebuffers( window, renderPass.handle, framebuffers );


	lut::CommandPool cpool = lut::create_command_pool( window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT );

	std::vector<VkCommandBuffer> cbuffers;
	std::vector<lut::Fence> cbfences;
	
	for( std::size_t i = 0; i < framebuffers.size(); ++i )
	{
		cbuffers.emplace_back( lut::alloc_command_buffer( window, cpool.handle ) );
		cbfences.emplace_back( lut::create_fence( window, VK_FENCE_CREATE_SIGNALED_BIT ) );
	}

	lut::Semaphore imageAvailable = lut::create_semaphore( window );
	lut::Semaphore renderFinished = lut::create_semaphore( window );

	// Application main loop
	bool recreateSwapchain = false;

	while( !glfwWindowShouldClose( window.window ) )
	{
		// Let GLFW process events.
		// glfwPollEvents() checks for events, processes them. If there are no
		// events, it will return immediately. Alternatively, glfwWaitEvents()
		// will wait for any event to occur, process it, and only return at
		// that point. The former is useful for applications where you want to
		// render as fast as possible, whereas the latter is useful for
		// input-driven applications, where redrawing is only needed in
		// reaction to user input (or similar).
		glfwPollEvents(); // or: glfwWaitEvents()

		// Recreate swap chain?
		if( recreateSwapchain )
		{
			//TODO: re-create swapchain and associated resources!
			vkDeviceWaitIdle(window.device);
			//recreate them
			auto const changes = recreate_swapchain(window);
			if (changes.changedFormat)
				renderPass = create_render_pass(window);
			framebuffers.clear();
			create_swapchain_framebuffers(window, renderPass.handle, framebuffers);
			if (changes.changedSize)
			{
				pipe = create_triangle_pipeline(window, renderPass.handle, pipeLayout.handle);
			}
			recreateSwapchain = false;
			continue;
		}

		//TODO: acquire swapchain image.
		std::uint32_t imageIndex = 0;
		auto const acquireRes = vkAcquireNextImageKHR(
			window.device,
			window.swapchain,
			std::numeric_limits<std::uint64_t>::max(),
			imageAvailable.handle,
			VK_NULL_HANDLE,
			&imageIndex
		);

		if (VK_SUBOPTIMAL_KHR == acquireRes || VK_ERROR_OUT_OF_DATE_KHR == acquireRes)
		{
			recreateSwapchain = true;
			continue;
		}

		if (VK_SUCCESS != acquireRes)
		{
			throw lut::Error("Unable to acquire enxt swapchain image\n"
				"vkAcquireNextImageKHR() returned %s", lut::to_string(acquireRes).c_str());
		}
		//TODO: wait for command buffer to be available
		assert(std::size_t(imageIndex) < cbfences.size());
		if(auto const res = vkWaitForFences(window.device, 1, &cbfences[imageIndex].handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to wait for command buffer fence %u\n"
				"vkWaitForFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}
		if(auto const res = vkResetFences(window.device, 1, &cbfences[imageIndex].handle); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to reset command buffer fence %u\n"
				"vkResetFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}
		//TODO: record and submit commands
		assert(std::size_t(imageIndex) < cbuffers.size());
		assert(std::size_t(imageIndex) < framebuffers.size());
		record_commands(
			cbuffers[imageIndex],
			renderPass.handle,
			framebuffers[imageIndex].handle,
			pipe.handle,
			window.swapchainExtent
		);
		submit_commands(
			window,
			cbuffers[imageIndex],
			cbfences[imageIndex].handle,
			imageAvailable.handle,
			renderFinished.handle
		);
		//TODO: present rendered images.
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderFinished.handle;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &window.swapchain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;
		auto const presentRes = vkQueuePresentKHR(window.presentQueue, &presentInfo);
		if(VK_SUBOPTIMAL_KHR == presentRes || VK_ERROR_OUT_OF_DATE_KHR == presentRes)
		{
			recreateSwapchain = true;
		}
		else if (VK_SUCCESS != presentRes)
		{
			throw lut::Error("Unable present swapchain image %u\n"
				"vkQueuePresentKHR() returned %s", imageIndex, lut::to_string(presentRes).c_str());
		}
	}
	
	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle( window.device );
	// SABOTAGE! 1
	//vrenderFinished.handle = VK_NULL_HANDLE;


	return 0;
}

catch( std::exception const& eErr )
{
	std::fprintf( stderr, "Exception: %s\n", eErr.what() );
	return 1;
}

namespace
{
	void glfw_callback_key_press( GLFWwindow* aWindow, int aKey, int /*aScanCode*/, int aAction, int /*aModifierFlags*/ )
	{
		if( GLFW_KEY_ESCAPE == aKey && GLFW_PRESS == aAction )
		{
			glfwSetWindowShouldClose( aWindow, GLFW_TRUE );
		}
	}
}

namespace
{
	lut::RenderPass create_render_pass( lut::VulkanWindow const& aWindow )
	{
		VkAttachmentDescription attachments[1]{};
		attachments[0].format = aWindow.swapchainFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference subpassAttachments[1]{};
		subpassAttachments[0].attachment = 0; // the zero refers to attachments[0] declared earlier.
		subpassAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1;
		subpasses[0].pColorAttachments = subpassAttachments;
		// This subpass only uses a single color attachment, and does not use any other attachmen types. We can 10
		// therefore leave many of the members at zero/nullptr. If this subpass used a depth attachment (=depth buffer), 11
		// we would specify this via the pDepthStencilAttachment member. 12
		// See the documentation for VkSubpassDescription for other attachment types and the use/meaning of those.

		VkSubpassDependency deps[1]{};
		deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; // == subpasses[0] declared above
		deps[0].srcAccessMask = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].dstSubpass = 0;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 1;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 1;
		passInfo.pDependencies = deps;
		VkRenderPass rpass = VK_NULL_HANDLE;
		if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create render pass\n"
				"vkCreateRenderPass() returned %s", lut::to_string(res).c_str());
		}
		return lut::RenderPass(aWindow.device, rpass);
	}

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext )
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 0;
		layoutInfo.pSetLayouts = nullptr;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create pipeline layout\n" "vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::PipelineLayout(aContext.device, layout);
	}
	lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout )
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);
		// Define shader stages in the pipeline
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";
		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		// Define which primitive (point, line, triangle, ...) the input is assembled into for rasterization.
		VkPipelineInputAssemblyStateCreateInfo assemblyInfo{};
		assemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assemblyInfo.primitiveRestartEnable = VK_FALSE;
		// Define viewport and scissor regions
		VkViewport viewport{};
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = float(aWindow.swapchainExtent.width);
		viewport.height = float(aWindow.swapchainExtent.height);
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		VkRect2D scissor{};
		scissor.offset = VkOffset2D{ 0, 0 };
		scissor.extent = VkExtent2D{ aWindow.swapchainExtent.width, aWindow.swapchainExtent.height };
		VkPipelineViewportStateCreateInfo viewportInfo{};
		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportInfo.viewportCount = 1;
		viewportInfo.pViewports = &viewport;
		viewportInfo.scissorCount = 1;
		viewportInfo.pScissors = &scissor;
		// Define rasterization options
		VkPipelineRasterizationStateCreateInfo rasterInfo{};
		rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterInfo.depthClampEnable = VK_FALSE;
		rasterInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f; // required. 
		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		// Define blend state
		// We define one blend state per color attachment - this example uses a single color attachment, so we only
		// need one. Right now, we dont do any blending, so we can ignore most of the members.
		VkPipelineColorBlendAttachmentState blendStates[1]{};
		blendStates[0].blendEnable = VK_FALSE;
		blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;
		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount = 2; // vertex + fragment stages
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr; // no tessellation
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = nullptr; // no depth or stencil buffers
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr; // no dynamic states
		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0; // first subpass of aRenderPass
		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}
		return lut::Pipeline(aWindow.device, pipe);
	}

	void create_swapchain_framebuffers( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers )
	{
		assert( aFramebuffers.empty() );
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		for (std::size_t i = 0; i < aWindow.swapViews.size(); ++i)
		{
			VkImageView attachments[1] =
			{
				aWindow.swapViews[i]
			};
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0; //normal framebuffer
			fbInfo.renderPass = aRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = attachments;
			fbInfo.width = aWindow.swapchainExtent.width;
			fbInfo.height = aWindow.swapchainExtent.height;
			fbInfo.layers = 1;
			VkFramebuffer fb = VK_NULL_HANDLE;
			if (auto const res = vkCreateFramebuffer(aWindow.device, &fbInfo, nullptr, &fb); VK_SUCCESS != res)
			{
				throw lut::Error("Unable to create framebuffer for swap chain image\n"
					"vkCreateFramebuffer() returned %s", lut::to_string(res).c_str());
			}
			aFramebuffers.emplace_back(lut::Framebuffer(aWindow.device, fb));
		}
		assert( aWindow.swapViews.size() == aFramebuffers.size() );
	}



	void record_commands( VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer, VkPipeline aGraphicsPipe, VkExtent2D const& aImageExtent )
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begInfo.pInheritanceInfo = nullptr;
		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n" "vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
		// Begin render pass
		VkClearValue clearValues[1]{};
		clearValues[0].color.float32[0] = 0.1f; // Clear to a dark gray background.
		clearValues[0].color.float32[1] = 0.1f; // If we were debugging, this would potentially
		clearValues[0].color.float32[2] = 0.1f; // help us see whether the render pass took
		clearValues[0].color.float32[3] = 1.f; // place, even if nothing else was drawn.
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPass;
		passInfo.framebuffer = aFramebuffer;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 1;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		// Begin drawing with our graphics pipeline
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		// Draw a triangle = three vertices
		vkCmdDraw(aCmdBuff, 3, 1, 0, 0);
		// End the render pass
		vkCmdEndRenderPass(aCmdBuff);
		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
	}

	void submit_commands( lut::VulkanContext const& aContext, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore )
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		VkPipelineStageFlags waitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &aCmdBuff;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &aWaitSemaphore;
		submitInfo.pWaitDstStageMask = &waitPipelineStages;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &aSignalSemaphore;
		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo, aFence); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to submit command buffer to queue\n"
				"vkQueueSubmit() returned %s", lut::to_string(res).c_str());
		}
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
