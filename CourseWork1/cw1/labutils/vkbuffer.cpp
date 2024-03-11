#include "vkbuffer.hpp"

#include <utility>

#include <cassert>

#include "error.hpp"
#include "to_string.hpp"



namespace labutils
{
	Buffer::Buffer() noexcept = default;

	Buffer::~Buffer()
	{
		if( VK_NULL_HANDLE != buffer )
		{
			assert( VK_NULL_HANDLE != mAllocator );
			assert( VK_NULL_HANDLE != allocation );
			vmaDestroyBuffer( mAllocator, buffer, allocation );
		}
	}

	Buffer::Buffer( VmaAllocator aAllocator, VkBuffer aBuffer, VmaAllocation aAllocation ) noexcept
		: buffer( aBuffer )
		, allocation( aAllocation )
		, mAllocator( aAllocator )
	{}

	Buffer::Buffer( Buffer&& aOther ) noexcept
		: buffer( std::exchange( aOther.buffer, VK_NULL_HANDLE ) )
		, allocation( std::exchange( aOther.allocation, VK_NULL_HANDLE ) )
		, mAllocator( std::exchange( aOther.mAllocator, VK_NULL_HANDLE ) )
	{}
	Buffer& Buffer::operator=( Buffer&& aOther ) noexcept
	{
		std::swap( buffer, aOther.buffer );
		std::swap( allocation, aOther.allocation );
		std::swap( mAllocator, aOther.mAllocator );
		return *this;
	}
}

namespace labutils
{
	Buffer create_buffer( Allocator const& aAllocator, VkDeviceSize aSize, VkBufferUsageFlags aBufferUsage, VmaAllocationCreateFlags aMemoryFlags, VmaMemoryUsage aMemoryUsage )
	{
		// en: Create Vulkan buffer creation info structure
		// zh: 创建 Vulkan 缓冲区创建信息结构体
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = aSize;
		bufferInfo.usage = aBufferUsage;

		// en: Create VMA allocation info structure
		// zh: 创建 VMA 内存分配信息结构体
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.flags = aMemoryFlags;
		allocInfo.usage = aMemoryUsage;

		// en: Initialize Vulkan buffer and VMA allocation handles
		// zh: 初始化 Vulkan 缓冲区和 VMA 内存分配句柄
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;

		// en: Attempt to create buffer and allocate memory using VMA
		// zh: 尝试使用 VMA 创建缓冲区并分配内存
		if (const auto res = vmaCreateBuffer(aAllocator.allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr); VK_SUCCESS != res)
		{
			// en: Throw an error if buffer creation fails
			// zh: 如果缓冲区创建失败，则抛出错误
			throw Error("Unable to allocate buffer.\n"
				"vmaCreateBuffer() returned %s", to_string(res).c_str());
		}

		// en: Return a Buffer object encapsulating the allocator, buffer, and allocation
		// zh: 返回一个封装了分配器、缓冲区和分配的 Buffer 对象
		return Buffer(aAllocator.allocator, buffer, allocation);

		//TODO-[DONE] (Section 2) implement me! 
	}
}
