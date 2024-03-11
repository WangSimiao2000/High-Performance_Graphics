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

#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_window.hpp"

#include "../labutils/angle.hpp"
using namespace labutils::literals;

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 
namespace lut = labutils;

#include "vertex_data.hpp"




namespace
{
	namespace cfg
	{
		// Compiled shader code for the graphics pipeline
		// See sources in exercise4/shaders/*. 
#		define SHADERDIR_ "assets/exercise4/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "triangle.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "triangle.frag.spv";
#		undef SHADERDIR_



		// General rule: with a standard 24 bit or 32 bit float depth buffer,
		// you can support a 1:1000 ratio between the near and far plane with
		// minimal depth fighting. Larger ratios will introduce more depth
		// fighting problems; smaller ratios will increase the depth buffer's
		// resolution but will also limit the view distance.
		constexpr float kCameraNear  = 0.1f;
		constexpr float kCameraFar   = 100.f;

		constexpr auto kCameraFov    = 60.0_degf;
	}

	// GLFW callbacks
	void glfw_callback_key_press( GLFWwindow*, int, int, int, int );

	// Uniform data
	namespace glsl
	{
		struct SceneUniform;

	}

	// Helpers:
	lut::RenderPass create_render_pass( lut::VulkanWindow const& );

	lut::DescriptorSetLayout create_scene_descriptor_layout( lut::VulkanWindow const& );
	lut::DescriptorSetLayout create_object_descriptor_layout( lut::VulkanWindow const& );

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& );
	lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout );

	void create_swapchain_framebuffers( 
		lut::VulkanWindow const&, 
		VkRenderPass,
		std::vector<lut::Framebuffer>&
	);

	void update_scene_uniforms(
		glsl::SceneUniform&,
		std::uint32_t aFramebufferWidth,
		std::uint32_t aFramebufferHeight
	);

	void record_commands( 
		VkCommandBuffer,
		VkRenderPass,
		VkFramebuffer,
		VkPipeline,
		VkExtent2D const&
	);
	void submit_commands(
		lut::VulkanWindow const&,
		VkCommandBuffer,
		VkFence,
		VkSemaphore,
		VkSemaphore
	);
	void present_results( 
		VkQueue, 
		VkSwapchainKHR, 
		std::uint32_t aImageIndex, 
		VkSemaphore,
		bool& aNeedToRecreateSwapchain
	);
}


int main() try
{
	// Create Vulkan Window
	auto window = lut::make_vulkan_window();

	// Configure the GLFW window
	glfwSetKeyCallback( window.window, &glfw_callback_key_press );

	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator( window );

	// Intialize resources
	lut::RenderPass renderPass = create_render_pass( window );

	//TODO- (Section 3) create scene descriptor set layout
	//TODO- (Section 4) create object descriptor set layout


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

	// Load data
	ColorizedMesh triangleMesh = create_triangle_mesh( window, allocator );

	//TODO- (Section 3) create scene uniform buffer with lut::create_buffer()

	//TODO- (Section 3) create descriptor pool

	//TODO- (Section 3) allocate descriptor set for uniform buffer
	//TODO- (Section 3) initialize descriptor set with vkUpdateDescriptorSets

	//TODO- (Section 4) load textures into image
	//TODO- (Section 4) create image view for texture image
	//TODO- (Section 4) create default texture sampler
	//TODO- (Section 4) allocate and initialize descriptor sets for texture

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
			//TODO: (Section 1) re-create swapchain and associated resources - see Exercise 3!
			recreateSwapchain = false;
		}

		//TODO- (Section 1) Exercise 3:
		//TODO- (Section 1)  - acquire swapchain image.
		//TODO- (Section 1)  - wait for command buffer to be available
		//TODO- (Section 1)  - record and submit commands
		//TODO- (Section 1)  - present rendered images (note: use the present_results() method)
	}

	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle( window.device );

	return 0;
}
catch( std::exception const& eErr )
{
	std::fprintf( stderr, "\n" );
	std::fprintf( stderr, "Error: %s\n", eErr.what() );
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
	void update_scene_uniforms( glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight )
	{
		//TODO- (Section 3) initialize SceneUniform members
	}
}

namespace
{
	lut::RenderPass create_render_pass( lut::VulkanWindow const& aWindow )
	{
		throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
	}

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext )
	{
		throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
	}


	lut::Pipeline create_triangle_pipeline( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout )
	{
		throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
	}

	void create_swapchain_framebuffers( lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers )
	{
		assert( aFramebuffers.empty() );

		throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!

		assert( aWindow.swapViews.size() == aFramebuffers.size() );
	}

	lut::DescriptorSetLayout create_scene_descriptor_layout( lut::VulkanWindow const& aWindow )
	{
		throw lut::Error( "Not yet implemented" ); //TODO- (Section 3) implement me!
	}
	lut::DescriptorSetLayout create_object_descriptor_layout( lut::VulkanWindow const& aWindow )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: (Section 4) implement me!
	}

	void record_commands( VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer, VkPipeline aGraphicsPipe, VkExtent2D const& aImageExtent )
	{
		throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!

	}

	void submit_commands( lut::VulkanWindow const& aWindow, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: (Section 1/Exercise 3) implement me!
	}

	void present_results( VkQueue aPresentQueue, VkSwapchainKHR aSwapchain, std::uint32_t aImageIndex, VkSemaphore aRenderFinished, bool& aNeedToRecreateSwapchain )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: (Section 1/Exercise 3) implement me!
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
