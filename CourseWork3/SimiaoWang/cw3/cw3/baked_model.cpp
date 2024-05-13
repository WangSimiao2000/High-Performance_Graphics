#include "baked_model.hpp"

#include <cstdio>
#include <cstring>
#include "../labutils/vkutil.hpp"
#include "../labutils/to_string.hpp"
#include "../labutils/error.hpp"
namespace lut = labutils;

namespace
{
	// See cw2-bake/main.cpp for more info
	constexpr char kFileMagic[16] = "\0\0COMP5822Mmesh";
	constexpr char kFileVariant[16] = "default-cw3";

	constexpr std::uint32_t kMaxString = 32 * 1024;

	// functions
	BakedModel load_baked_model_(FILE*, char const*);
}

BakedModel load_baked_model(char const* aModelPath)
{
	FILE* fin = std::fopen(aModelPath, "rb");
	if (!fin)
		throw lut::Error("load_baked_model(): unable to open '%s' for reading", aModelPath);

	try
	{
		auto ret = load_baked_model_(fin, aModelPath);
		std::fclose(fin);
		return ret;
	}
	catch (...)
	{
		std::fclose(fin);
		throw;
	}
}

TMesh split_meshes(const labutils::VulkanContext& aContext, const labutils::Allocator& aAllocator, std::vector<glm::vec3> vertexDate, std::vector<glm::vec2> texCoords, std::vector<glm::vec3> normals, std::vector<std::uint32_t> indices)
{
	auto vertexSize = sizeof(glm::vec3) * vertexDate.size();
	auto texSize = sizeof(glm::vec2) * texCoords.size();
	auto normalSize = sizeof(glm::vec3) * normals.size();
	//std::cout << "tangent size " << tangentSize << std::endl;
	auto indexSize = sizeof(std::uint32_t) * indices.size();

	lut::Buffer VertexPosGPU = lut::create_buffer(
		aAllocator,
		vertexSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	lut::Buffer VertexColGPU = lut::create_buffer(
		aAllocator,
		texSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	lut::Buffer VertexNorGPU = lut::create_buffer(
		aAllocator,
		normalSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	lut::Buffer VertexIdxGPU = lut::create_buffer(
		aAllocator,
		indexSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);


	lut::Buffer posStaging = lut::create_buffer(
		aAllocator,
		vertexSize,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);
	lut::Buffer colStaging = lut::create_buffer(
		aAllocator,
		texSize,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);
	lut::Buffer norStaging = lut::create_buffer(
		aAllocator,
		normalSize,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);
	lut::Buffer idxStaging = lut::create_buffer(
		aAllocator,
		indexSize,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_MEMORY_USAGE_GPU_TO_CPU
	);


	void* posPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, posStaging.allocation, &posPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\n"
			"vmaMapMemory() returned %s", lut::to_string(res).c_str());
	}
	std::memcpy(posPtr, vertexDate.data(), vertexSize);
	vmaUnmapMemory(aAllocator.allocator, posStaging.allocation);
	void* colPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, colStaging.allocation, &colPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\n"
			"vmaMapMemory() returned %s", lut::to_string(res).c_str());
	}
	std::memcpy(colPtr, texCoords.data(), texSize);
	vmaUnmapMemory(aAllocator.allocator, colStaging.allocation);
	void* norPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, norStaging.allocation, &norPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\n"
			"vmaMapMemory() returned %s", lut::to_string(res).c_str());
	}
	std::memcpy(norPtr, normals.data(), normalSize);
	vmaUnmapMemory(aAllocator.allocator, norStaging.allocation);
	void* idxPtr = nullptr;
	if (auto const res = vmaMapMemory(aAllocator.allocator, idxStaging.allocation, &idxPtr); VK_SUCCESS != res)
	{
		throw lut::Error("Mapping memory for writing\n"
			"vmaMapMemory() returned %s", lut::to_string(res).c_str());
	}
	std::memcpy(idxPtr, indices.data(), indexSize);
	vmaUnmapMemory(aAllocator.allocator, idxStaging.allocation);


	lut::Fence uploadComplete = create_fence(aContext);
	// Queue data uploads from staging buffers to the final buffers 
	// This uses a separate command pool for simplicity. 
	lut::CommandPool uploadPool = lut::create_command_pool(aContext);
	VkCommandBuffer uploadCmd = lut::alloc_command_buffer(aContext, uploadPool.handle);
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;
	if (auto const res = vkBeginCommandBuffer(uploadCmd, &beginInfo); VK_SUCCESS != res)
	{
		throw lut::Error("Beginning command buffer recording\n"
			"vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
	}
	VkBufferCopy pcopy{};
	pcopy.size = vertexSize;
	vkCmdCopyBuffer(uploadCmd, posStaging.buffer, VertexPosGPU.buffer, 1, &pcopy);
	lut::buffer_barrier(uploadCmd,
		VertexPosGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);
	VkBufferCopy ccopy{};
	ccopy.size = texSize;
	vkCmdCopyBuffer(uploadCmd, colStaging.buffer, VertexColGPU.buffer, 1, &ccopy);
	lut::buffer_barrier(uploadCmd,
		VertexColGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);
	VkBufferCopy ncopy{};
	ncopy.size = normalSize;
	vkCmdCopyBuffer(uploadCmd, norStaging.buffer, VertexNorGPU.buffer, 1, &ncopy);
	lut::buffer_barrier(uploadCmd,
		VertexNorGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);
	VkBufferCopy icopy{};
	icopy.size = indexSize;
	vkCmdCopyBuffer(uploadCmd, idxStaging.buffer, VertexIdxGPU.buffer, 1, &icopy);
	lut::buffer_barrier(uploadCmd,
		VertexIdxGPU.buffer,
		VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
	);


	if (auto const res = vkEndCommandBuffer(uploadCmd); VK_SUCCESS != res)
	{
		throw lut::Error("Ending command buffer recording\n"
			"vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
	}

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &uploadCmd;
	if (auto const res = vkQueueSubmit(aContext.graphicsQueue, 1, &submitInfo, uploadComplete.handle); VK_SUCCESS != res)
	{
		throw lut::Error("Submitting commands\n"
			"vkQueueSubmit() returned %s", lut::to_string(res).c_str());
	}
	if (auto const res = vkWaitForFences(aContext.device, 1, &uploadComplete.handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
	{
		throw lut::Error("Waiting for upload to complete\n"
			"vkWaitForFences() returned %s", lut::to_string(res).c_str());
	}

	return TMesh{
		std::move(VertexPosGPU),
		std::move(VertexColGPU),
		std::move(VertexNorGPU),
		std::move(VertexIdxGPU) // now three floats per position 4
	};
}

namespace
{
	void checked_read_(FILE* aFin, std::size_t aBytes, void* aBuffer)
	{
		auto ret = std::fread(aBuffer, 1, aBytes, aFin);

		if (aBytes != ret)
			throw lut::Error("checked_read_(): expected %zu bytes, got %zu", aBytes, ret);
	}

	std::uint32_t read_uint32_(FILE* aFin)
	{
		std::uint32_t ret;
		checked_read_(aFin, sizeof(std::uint32_t), &ret);
		return ret;
	}
	std::string read_string_(FILE* aFin)
	{
		auto const length = read_uint32_(aFin);

		if (length >= kMaxString)
			throw lut::Error("read_string_(): unexpectedly long string (%u bytes)", length);

		std::string ret;
		ret.resize(length);

		checked_read_(aFin, length, ret.data());
		return ret;
	}

	BakedModel load_baked_model_(FILE* aFin, char const* aInputName)
	{
		BakedModel ret;

		// Figure out base path
		char const* pathBeg = aInputName;
		char const* pathEnd = std::strrchr(pathBeg, '/');

		std::string const prefix = pathEnd
			? std::string(pathBeg, pathEnd + 1)
			: ""
			;

		// Read header and verify file magic and variant
		char magic[16];
		checked_read_(aFin, 16, magic);

		if (0 != std::memcmp(magic, kFileMagic, 16))
			throw lut::Error("load_baked_model_(): %s: invalid file signature!", aInputName);

		char variant[16];
		checked_read_(aFin, 16, variant);

		if (0 != std::memcmp(variant, kFileVariant, 16))
			throw lut::Error("load_baked_model_(): %s: file variant is '%s', expected '%s'", aInputName, variant, kFileVariant);

		// Read texture info
		auto const textureCount = read_uint32_(aFin);
		for (std::uint32_t i = 0; i < textureCount; ++i)
		{
			BakedTextureInfo info;
			info.path = prefix + read_string_(aFin);

			std::uint8_t channels;
			checked_read_(aFin, sizeof(std::uint8_t), &channels);
			info.channels = channels;

			ret.textures.emplace_back(std::move(info));
		}

		// Read material info
		auto const materialCount = read_uint32_(aFin);
		for (std::uint32_t i = 0; i < materialCount; ++i)
		{
			BakedMaterialInfo info;
			info.baseColorTextureId = read_uint32_(aFin);
			info.roughnessTextureId = read_uint32_(aFin);
			info.metalnessTextureId = read_uint32_(aFin);
			info.alphaMaskTextureId = read_uint32_(aFin);
			info.normalMapTextureId = read_uint32_(aFin);
			info.emissiveTextureId = read_uint32_(aFin);

			assert(info.baseColorTextureId < ret.textures.size());
			assert(info.roughnessTextureId < ret.textures.size());
			assert(info.metalnessTextureId < ret.textures.size());
			assert(info.emissiveTextureId < ret.textures.size());

			ret.materials.emplace_back(std::move(info));
		}

		// Read mesh data
		auto const meshCount = read_uint32_(aFin);
		for (std::uint32_t i = 0; i < meshCount; ++i)
		{
			BakedMeshData data;
			data.materialId = read_uint32_(aFin);
			assert(data.materialId < ret.materials.size());

			auto const V = read_uint32_(aFin);
			auto const I = read_uint32_(aFin);

			data.positions.resize(V);
			checked_read_(aFin, V * sizeof(glm::vec3), data.positions.data());

			data.normals.resize(V);
			checked_read_(aFin, V * sizeof(glm::vec3), data.normals.data());

			data.texcoords.resize(V);
			checked_read_(aFin, V * sizeof(glm::vec2), data.texcoords.data());


			data.indices.resize(I);
			checked_read_(aFin, I * sizeof(std::uint32_t), data.indices.data());

			ret.meshes.emplace_back(std::move(data));
		}

		// Check
		char byte;
		auto const check = std::fread(&byte, 1, 1, aFin);

		if (0 != check)
			std::fprintf(stderr, "Note: '%s' contains trailing bytes\n", aInputName);

		return ret;
	}
}
