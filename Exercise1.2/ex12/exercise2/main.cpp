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
	//TODO-[DONE]: wait with vkWaitForFences
	// Commands are executed asynchronously. Before we can access the result, we need to wait for the processing
	// to finish. The fence that we passed to VkQueueSubmit() will become signalled when the command buffer has
	// finished processing ¨C we will wait for that to occur with vkWaitForFences().
	
	// Wait for commands to execute
	constexpr std::uint64_t kMaxWait = std::numeric_limits<std::uint64_t>::max();
	
	if(auto const res = vkWaitForFences(context.device, 1, &fence.handle, VK_TRUE, kMaxWait); VK_SUCCESS != res)
	{
		throw lut::Error("Waiting for fence\n"
			"vkWaitForFences() returned %s", lut::to_string(res).c_str()
			);
	}

	// Access image and write it to disk.
	//TODO-[DONE]: access buffer data with vkMapMemory()
	// We copied the image to the buffer dlBuffer - we allocated the memory backing this buffer, dlMemory, from a
	// memory type with the property VK MEMORY PROPERTY HOST VISIBLE BIT. This means that we can
	// ¡°map¡± the contents of this buffer such that it becomes accessible through a normal C/C++ pointer.
	void * dataPtr = nullptr;
	if(auto const res = vkMapMemory(context.device, dlBuffer.memory, 0, cfg::kImageSize, 0, &dataPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory\n"
			"vkMapMemory() returned %s", lut::to_string(res).c_str()
		);
	}

	assert(dataPtr);
	
	std::vector<std::byte> buffer(cfg::kImageSize);
	std::memcpy(buffer.data(), dataPtr, cfg::kImageSize);
	
	// Why the extra copy? dataPtr points into a special memory region. This memory region may be created
	// uncached (e.g., reads bypass CPU caches). Streaming out of such memory is OK - so memcpy will touch
	// each byte exactly once. Reading multiple times from the memory, which the compression method likely does,
	// is significantly more expensive.
	//
	// In one test, passing dataPtr directly to stbi write png() takes about 4.5s, whereas using the extra buffer
	// reduces this time to 0.5s.
	//
	// To avoid the extra copy, we could request memory with the VK MEMORY PROPERTY HOST CACHED
	// property in addition. However, not all devices support this, and it may have other overheads (i.e., the
	// device/driver likely needs to snoop on the CPU caches, similar to HOST COHERENT).
	
	vkUnmapMemory(context.device, dlBuffer.memory);

	//TODO-[DONE]: write image data to disk with stbi_write_png()
	if (!stbi_write_png(cfg::kImageOutput, cfg::kImageWidth, cfg::kImageHeight, 4, buffer.data(), cfg::kImageWidth * 4))
	{
		throw lut::Error("Unable to write image: stbi_write_png() returned error");
	}
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
		// Note: the stencilLoadOp & stencilStoreOp members are left initialized to 0 (=DONT CARE). The image
		// format (R8G8B8A8 SRGB) of the color attachment does not have a stencil component, so these are ignored
		// either way.
		VkAttachmentDescription attachments[1]{};
		attachments[0].format = cfg::kImageFormat; // VK FORMAT R8G8B8A8 SRGB
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		VkAttachmentReference subpassAttachments[1]{};
		subpassAttachments[0].attachment = 0; // the zero refers to attachments[0] declared earlier.
		subpassAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		
		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1;
		subpasses[0].pColorAttachments = subpassAttachments;
		
		// This subpass only uses a single color attachment, and does not use any other attachmen types. We can
		// therefore leave many of the members at zero/nullptr. If this subpass used a depth attachment (=depth buffer),
		// we would specify this via the pDepthStencilAttachment member.
		// 
		// See the documentation for VkSubpassDescription for other attachment types and the use/meaning of those

		VkSubpassDependency deps[1]{};
		deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[0].srcSubpass = 0; // == subpasses[0] declared above
		deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 1;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 1;
		passInfo.pDependencies = deps;
		
		VkRenderPass rpass = VK_NULL_HANDLE;
		if(auto const res = vkCreateRenderPass(aContext.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create render pass\n"
				"vkCreateRenderPass() returned %s", lut::to_string(res).c_str()
			);
		}
		
		return lut::RenderPass(aContext.device, rpass);
		//TODO-[DONE]: implement me!
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
