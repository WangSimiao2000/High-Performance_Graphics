#include "vkimage.hpp"

#include <limits>
#include <vector>
#include <utility>
#include <algorithm>

#include <cstdio>
#include <cassert>
#include <cstring> // for std::memcpy()

#include <stb_image.h>

#include "error.hpp"
#include "vkutil.hpp"
#include "vkbuffer.hpp"
#include "to_string.hpp"



namespace
{
	// Unfortunately, std::countl_zero() isn't available in C++17; it was added
	// in C++20. This provides a fallback implementation. Unlike C++20, this
	// returns a std::uint32_t and not a signed int.
	//
	// See https://graphics.stanford.edu/~seander/bithacks.html for this and
	// other methods like it.
	//
	// Note: that this is unlikely to be the most efficient implementation on
	// most processors. Many instruction sets have dedicated instructions for
	// this operation. E.g., lzcnt (x86 ABM/BMI), bsr (x86).
	inline 
	std::uint32_t countl_zero_( std::uint32_t aX )
	{
		if( !aX ) return 32;

		uint32_t res = 0;

		if( !(aX & 0xffff0000) ) (res += 16), (aX <<= 16);
		if( !(aX & 0xff000000) ) (res +=  8), (aX <<=  8);
		if( !(aX & 0xf0000000) ) (res +=  4), (aX <<=  4);
		if( !(aX & 0xc0000000) ) (res +=  2), (aX <<=  2);
		if( !(aX & 0x80000000) ) (res +=  1);

		return res;
	}
}

namespace labutils
{
	Image::Image() noexcept = default;

	Image::~Image()
	{
		if( VK_NULL_HANDLE != image )
		{
			assert( VK_NULL_HANDLE != mAllocator );
			assert( VK_NULL_HANDLE != allocation );
			vmaDestroyImage( mAllocator, image, allocation );
		}
	}

	Image::Image( VmaAllocator aAllocator, VkImage aImage, VmaAllocation aAllocation ) noexcept
		: image( aImage )
		, allocation( aAllocation )
		, mAllocator( aAllocator )
	{}

	Image::Image( Image&& aOther ) noexcept
		: image( std::exchange( aOther.image, VK_NULL_HANDLE ) )
		, allocation( std::exchange( aOther.allocation, VK_NULL_HANDLE ) )
		, mAllocator( std::exchange( aOther.mAllocator, VK_NULL_HANDLE ) )
	{}
	Image& Image::operator=( Image&& aOther ) noexcept
	{
		std::swap( image, aOther.image );
		std::swap( allocation, aOther.allocation );
		std::swap( mAllocator, aOther.mAllocator );
		return *this;
	}
}

namespace labutils
{
	Image load_image_texture2d( char const* aPath, VulkanContext const& aContext, VkCommandPool aCmdPool, Allocator const& aAllocator )
	{
		// en: Flip images vertically by default. Vulkan expects the first scanline to be the bottom-most scanline. PNG et al.
		// zh: 默认情况下垂直翻转图像。Vulkan 期望第一扫描行是最底部的扫描行。PNG等格式。
		// en: instead define the first scanline to be the top-most one.
		// zh: 而是定义第一扫描行为最顶部的扫描行。
		stbi_set_flip_vertically_on_load(1);

		// en: load base image
		// zh: 加载基础图像
		int baseWidthi, baseHeighti, baseChannelsi;
		stbi_uc* data = stbi_load(aPath, &baseWidthi, &baseHeighti, &baseChannelsi, 4);
		
		if (!data)
		{
			throw Error("%s: unable to load texture base image (%s)", aPath, 0, stbi_failure_reason());
		}

		auto const baseWidth = std::uint32_t(baseWidthi);
		auto const baseHeight = std::uint32_t(baseHeighti);
		
		// en: Create staging buffer and copy image data to it
		// zh: 创建临时缓冲区，并将图像数据复制到其中
		auto const sizeInBytes = baseWidth * baseHeight * 4;

		auto staging = create_buffer(aAllocator, sizeInBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		
		void* sptr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, staging.allocation, &sptr); VK_SUCCESS != res)
		{
			throw Error("Mapping memory for writing\n"
				"vmaMapMemory() returned %s", to_string(res).c_str());
		}
		
		std::memcpy(sptr, data, sizeInBytes);
		vmaUnmapMemory(aAllocator.allocator, staging.allocation);
		
		// en: Free image data
		// zh: 释放图像数据(释放内存)
		stbi_image_free(data);

		// en: Create image
		// zh: 使用 create_image_texture2d 函数创建图像
		Image ret = create_image_texture2d(aAllocator, baseWidth, baseHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

		// en: Create command buffer for data upload and begin recording
		// zh: 创建用于数据上传的命令缓冲区，并开始记录
		VkCommandBuffer cbuff = alloc_command_buffer(aContext, aCmdPool);
		
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0;
		beginInfo.pInheritanceInfo = nullptr;
		
		if (auto const res = vkBeginCommandBuffer(cbuff, &beginInfo); VK_SUCCESS != res)
		{
			throw Error("Beginning command buffer recording\n"
				"vkBeginCommandBuffer() returned %s", to_string(res).c_str());
		}

		// Transition whole image layout
		// When copying data to the image, the image’s layout must be TRANSFER DST OPTIMAL. The current
		// image layout is UNDEFINED (which is the initial layout the image was created in).
		auto const mipLevels = compute_mip_level_count(baseWidth, baseHeight);
		image_barrier(cbuff, ret.image,
			0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);
		
		// en: Upload data from staging buffer to image
		// zh: 将数据从暂存缓冲区上传到图像
		VkBufferImageCopy copy;
		copy.bufferOffset = 0;
		copy.bufferRowLength = 0;
		copy.bufferImageHeight = 0;
		copy.imageSubresource = VkImageSubresourceLayers{
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,
			0,1
		};
		copy.imageOffset = VkOffset3D{ 0,0,0 };
		copy.imageExtent = VkExtent3D{ baseWidth,baseHeight,1 };
		
		vkCmdCopyBufferToImage(cbuff, staging.buffer, ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		
		// en: Transition base level to TRANSFER SRC OPTIMAL
		// zh: 将基本级别转换为TRANSFER SRC OPTIMAL
		// note: TRANSFER SRC OPTIMAL 是 Vulkan 图像布局中的一种布局类型。它表示图像被优化用于作为传输源（TRANSFER SOURCE），即可以作为数据传输的源头。TRANSFER SRC OPTIMAL 布局通常用于数据传输操作，例如从图像复制到缓冲区或图像之间的复制。
		image_barrier(cbuff, ret.image,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1,
				0, 1
			}
		);

		// en: Process all mipmap levels
		// zh: 处理所有的 mipmap 级别
		std::uint32_t width = baseWidth, height = baseHeight;

		for (std::uint32_t level = 1; level < mipLevels; ++level)
		{
			// en: Blit previous mipmap level(= level - 1) to the current level.Note that the loop starts at level = 1.
			// zh: 将前一个 mipmap 级别（= level - 1）复制到当前级别。请注意，循环从 level = 1 开始。
			// en: evel = 0 is the base level that we initialied before the loop.
			// zh: level = 0 是我们在循环之前初始化的基本级别。
			VkImageBlit blit{};
			blit.srcSubresource = VkImageSubresourceLayers{
				VK_IMAGE_ASPECT_COLOR_BIT,
				level - 1,
				0,1
			};
			blit.srcOffsets[0] = { 0,0,0 };
			blit.srcOffsets[1] = { std::int32_t(width),std::int32_t(height),1 };
			
			// en: Next mip level
			// zh: 下一个 mip 级别
			width >>= 1; if (width == 0) width = 1;
			height >>= 1; if (height == 0) height = 1;

			blit.dstSubresource = VkImageSubresourceLayers{
				VK_IMAGE_ASPECT_COLOR_BIT,
				level,
				0,1
			};

			blit.dstOffsets[0] = { 0,0,0 };
			blit.dstOffsets[1] = { std::int32_t(width),std::int32_t(height),1 };
			
			vkCmdBlitImage(cbuff,
				ret.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				ret.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR
			);

			// en: Transition mip level to TRANSFER SRC OPTIMAL for the next iteration. (Technically this is unnecessary for the last mip level, but transitioning it as well simplifes the final barrier following the loop)
			// zh: 为下一次迭代将 mip 级别转换为 TRANSFER SRC OPTIMAL (从技术上讲，对于最后一个 mip 级别这是不必要的，但对它进行转换也简化了循环后的最终屏障)
			image_barrier(cbuff, ret.image,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VkImageSubresourceRange{
					VK_IMAGE_ASPECT_COLOR_BIT,
					level, 1,
					0, 1
				}
			);
		}

		// en: Whole image is currently in the TRANSFER SRC OPTIMAL layout. To use the image as a texture from which we sample, it must be in the SHADER READ ONLY OPTIMAL layout.
		// zh: 整个图像当前处于TRANSFER SRC OPTIMAL布局。要将图像用作我们采样的纹理，它必须处于SHADER READ ONLY OPTIMAL布局。
		image_barrier(cbuff, ret.image,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0, mipLevels,
				0, 1
			}
		);

		// en: End command recording
		// zh: 结束命令记录
		if (auto const res = vkEndCommandBuffer(cbuff); VK_SUCCESS != res)
		{
			throw Error("Ending command buffer recording\n"
				"vkEndCommandBuffer() returned %s", to_string(res).c_str());
		}

		// en: Submit command buffer and wait for commands to complete. Commands must have completed before we can destroy the temporary resources, such as the staging buffers
		// zh: 提交命令缓冲区并等待命令完成。在我们可以销毁临时资源（如临时缓冲区）之前，命令必须已经完成。
		Fence uploadComplete = create_fence(aContext);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cbuff;

		if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo, uploadComplete.handle); VK_SUCCESS != res)
		{
			throw Error("Submitting commands\n"
				"vkQueueSubmit() returned %s", to_string(res).c_str());
		}

		if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw Error("Waiting for upload to complete\n"
				"vkWaitForFences() returned %s", to_string(res).c_str());
		}

		// Return resulting image
		// en: Most temporary resources are destroyed automatically through their destructors. However, the command buffer we must free manually.
		// zh: 大多数临时资源会通过它们的析构函数自动销毁。然而，我们必须手动释放命令缓冲区。
		vkFreeCommandBuffers(aContext.device, aCmdPool, 1, &cbuff);

		return ret;
		//TODO-[DONE] (Section 4) implement me! 
	}

	Image create_image_texture2d( Allocator const& aAllocator, std::uint32_t aWidth, std::uint32_t aHeight, VkFormat aFormat, VkImageUsageFlags aUsage )
	{
		auto const mipLevels = compute_mip_level_count(aWidth, aHeight);
		
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = aFormat;
		imageInfo.extent.width = aWidth;
		imageInfo.extent.height = aHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = aUsage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.flags = 0;
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		
		if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr); VK_SUCCESS != res)
		{
			throw Error("Unable to allocate image.\n"
				"vmaCreateImage() returned %s", to_string(res).c_str());
		}
		
		return Image(aAllocator.allocator, image, allocation);

		//TODO-[DONE] (Section 4) implement me!
	}

	std::uint32_t compute_mip_level_count( std::uint32_t aWidth, std::uint32_t aHeight )
	{
		std::uint32_t const bits = aWidth | aHeight;
		std::uint32_t const leadingZeros = countl_zero_( bits );
		return 32-leadingZeros;
	}
}
