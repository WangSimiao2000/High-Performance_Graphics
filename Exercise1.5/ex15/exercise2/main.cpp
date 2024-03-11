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

#include <stb_image_write.h>

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_context.hpp"
namespace lut = labutils;

#include "image.hpp"
#include "buffer.hpp"


namespace
{
	namespace cfg
	{
		// Image format:
		// Vulkan implementation do not have to support all image formats
		// listed in the VkFormat enum. Rather, some formats may only be used
		// with some operations. We are rendering (without blending!), so our
		// use case is VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT (with blending it
		// would be VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT). We need a
		// format that supports VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT (with
		// optimal tiling). Vulkan requires some formats to be supported - see
		// https://www.khronos.org/registry/vulkan/specs/1.3/html/chap34.html#features-required-format-support
		//
		// It turns out that support there are no mandatory formats listed when
		// rendering with blending disabled. However, it seems that commonly
		// allow R8G8B8A8_SRGB for this use case anyway:
		// http://vulkan.gpuinfo.org/listoptimaltilingformats.php
		constexpr VkFormat kImageFormat = VK_FORMAT_R8G8B8A8_SRGB;

		// Image size and related parameters
		constexpr std::uint32_t kImageWidth = 1280;
		constexpr std::uint32_t kImageHeight = 720;
		constexpr std::uint32_t kImageSize = kImageWidth * kImageHeight * 4; // RGBA

		// Output image path
		constexpr char const* kImageOutput = "output.png";

		// Compiled shader code for the graphics pipeline
		// See sources in exercise2/shaders/*. 
#		define SHADERDIR_ "assets/exercise2/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "triangle.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "triangle.frag.spv";
#		undef SHADERDIR_
	}

	// Helpers:
	lut::RenderPass create_render_pass( lut::VulkanContext const& );

	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& );
	lut::Pipeline create_triangle_pipeline( lut::VulkanContext const&, VkRenderPass, VkPipelineLayout );


	std::tuple<Image,lut::ImageView> create_framebuffer_image( lut::VulkanContext const& );
	lut::Framebuffer create_framebuffer( lut::VulkanContext const&, VkRenderPass, VkImageView );

	Buffer create_download_buffer( lut::VulkanContext const& );

	void record_commands( 
		VkCommandBuffer,
		VkRenderPass,
		VkFramebuffer,
		VkPipeline,
		VkImage,
		VkBuffer
	);
	void submit_commands(
		lut::VulkanContext const&,
		VkCommandBuffer,
		VkFence
	);

	std::uint32_t find_memory_type( lut::VulkanContext const&, std::uint32_t aMemoryTypeBits, VkMemoryPropertyFlags );
}

int main() try
{
	// Create the Vulkan instance, set up the validation, select a physical
	// device and instantiate a logical device from the selected device.
	// Request a single GRAPHICS queue for now, and fetch this from the created
	// logical device.
	//
	// See Exercise 1.1 for a detailed walkthrough of this process.
	lut::VulkanContext context = lut::make_vulkan_context();

	//TODO-maybe: experiment with SAMPLE_COUNT != 1
	
	// To render an image, we need a number of Vulkan resources. The following
	// creates these:
	lut::RenderPass renderPass = create_render_pass( context );

	lut::PipelineLayout pipeLayout = create_triangle_pipeline_layout( context );
	lut::Pipeline pipe = create_triangle_pipeline( context, renderPass.handle, pipeLayout.handle );

	auto [fbImage, fbImageView] = create_framebuffer_image( context );
	lut::Framebuffer framebuffer = create_framebuffer( context, renderPass.handle, fbImageView.handle );

	Buffer dlBuffer = create_download_buffer( context );

	lut::CommandPool cpool = lut::create_command_pool( context );
	VkCommandBuffer cbuffer = lut::alloc_command_buffer( context, cpool.handle );

	lut::Fence fence = lut::create_fence( context );

	// Now that we have set up the necessary resources, we can use our Vulkan
	// device to render the image. This happens in two steps:
	//  
	// 1. Record rendering commands in to the command buffer that we've 
	//    created for this purpose.
	// 2. Submit the command buffer to the Vulkan device / GPU for processing.

	record_commands(
		cbuffer,
		renderPass.handle,
		framebuffer.handle,
		pipe.handle,
		fbImage.image,
		dlBuffer.buffer
	);

	submit_commands(
		context,
		cbuffer,
		fence.handle
	);

	// Commands are executed asynchronously. Before we can access the result,
	// we need to wait for the processing to finish. The fence that we passed
	// to VkQueueSubmit() will become signalled when the command buffer has
	// finished processing -- we will wait for that to occur with
	// vkWaitForFences().

	// Wait for commands to finish executing
	//TODO: wait with vkWaitForFences

	// Access image and write it to disk.
	//TODO: access buffer data with vkMapMemory()

	//TODO: write image data to disk with stbi_write_png()

	// Cleanup
	// None required :-) The C++ wrappers take care of destroying the various
	// objects as the wrappers go out of scpe at the end of main()!

	return 0;
}
catch( std::exception const& eErr )
{
	std::fprintf( stderr, "Exception: %s\n", eErr.what() );
	return 1;
}


namespace
{
	lut::RenderPass create_render_pass( lut::VulkanContext const& aContext )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}


	lut::PipelineLayout create_triangle_pipeline_layout( lut::VulkanContext const& aContext )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!

	}
	lut::Pipeline create_triangle_pipeline( lut::VulkanContext const& aContext, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}


	std::tuple<Image, lut::ImageView> create_framebuffer_image( lut::VulkanContext const& aContext )
	{
		//Image image( aContext.device );

		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}


	lut::Framebuffer create_framebuffer( lut::VulkanContext const& aContext, VkRenderPass aRenderPass, VkImageView aTargetImageView )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	Buffer create_download_buffer( lut::VulkanContext const& aContext )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	void record_commands( VkCommandBuffer aCmdBuff, VkRenderPass aRenderPass, VkFramebuffer aFramebuffer, VkPipeline aGraphicsPipe, VkImage aFbImage, VkBuffer aDownloadBuffer )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

	void submit_commands( lut::VulkanContext const& aContext, VkCommandBuffer aCmdBuff, VkFence aFence )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}

}

namespace
{
	std::uint32_t find_memory_type( lut::VulkanContext const& aContext, std::uint32_t aMemoryTypeBits, VkMemoryPropertyFlags aProps )
	{
		throw lut::Error( "Not yet implemented" ); //TODO: implement me!
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
