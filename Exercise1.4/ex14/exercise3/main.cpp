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
			
			recreateSwapchain = false;
			continue;
		}

		//TODO: acquire swapchain image.
		//TODO: wait for command buffer to be available
		//TODO: record and submit commands
		//TODO: present rendered images.
	}
	
	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle( window.device );

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
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}
	lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	void create_swapchain_framebuffers( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers )
	{
		assert( aFramebuffers.empty() );

		throw lut::Error( "Not yet implemented" ); //TODO: implement me!

		assert( aWindow.swapViews.size() == aFramebuffers.size() );
	}



	void record_commands( VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer, VkPipeline aGraphicsPipe, VkExtent2D const& aImageExtent )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	void submit_commands( lut::VulkanContext const& aContext, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
