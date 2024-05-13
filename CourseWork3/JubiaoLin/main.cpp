#include <tuple>
#include <chrono>
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

#include <volk/volk.h>

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

#include "baked_model.hpp"
#include <iostream>


namespace
{
	namespace cfg 
	{
#define SHADERDIR_ "assets/cw3/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
		constexpr char const* sVertShaderPath = SHADERDIR_ "fullscreen.vert.spv";
		constexpr char const* sFragShaderPath = SHADERDIR_ "fullscreen.frag.spv";
#undef SHADERDIR_
		constexpr float kCameraNear = 0.1f;
		constexpr float kCameraFar = 100.f;
		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
		constexpr auto kCameraFov = 60.0_degf;
		constexpr float kCameraBaseSpeed = 1.7f;
		constexpr float kCameraFastMult = 5.f;
		constexpr float kCameraSlowMult = 0.05f;
		constexpr float kCameraMouseSensitivity = 0.01f;
	}

	namespace glsl
	{
		struct SceneUniform {
			// Note: need to be careful about the packing/alignment here!
			glm::mat4 camera;
			glm::mat4 projection;
			glm::mat4 projCam;
			glm::vec3 cameraPos;
			float _p0;
			glm::vec3 lightPos;
			float _p1;
			glm::vec3 lightCol;
			float _p2;
		};
		// We want to use vkCmdUpdateBuffer() to update the contents of our uniform buffers. vkCmdUpdateBuffer()
		// has a number of requirements, including the two below.
		static_assert(sizeof(SceneUniform) <= 65536, "SceneUniform must be less than 65536 bytes for vkCmdUpdateBuffer");
		static_assert(sizeof(SceneUniform) % 4 == 0, "SceneUniform size must be a multiple of 4 bytes");
	}

	enum class EInputState
	{
		forward,
		backward,
		strafeLeft,
		strafeRight,
		levitate,
		sink,
		fast,
		slow,
		mousing,
		max
	};

	struct  UserState
	{
		bool inputMap[std::size_t(EInputState::max)] = {};
		float mouseX = 0.f, mouseY = 0.f;
		float previousX = 0.f, previousY = 0.f;
		bool wasMousing = false;
		glm::vec3 lightPos = glm::vec3(-0.2972, 7.3100, -11.9532);
		glm::vec3 lightCol = glm::vec3(1.0, 1.0, 1.0);
		glm::mat4 camera2world = glm::identity<glm::mat4>();
		
	};
	// Local types/structures:

	struct ImageAttachment {
		VkImage image;
		VmaAllocation allocation;
		VkImageView view;
	};

	// Local functions:
	using Clock_ = std::chrono::steady_clock;
	using Secondsf_ = std::chrono::duration<float, std::ratio<1>>;
	void glfw_callback_button(GLFWwindow*, int, int, int);
	void glfw_callback_motion(GLFWwindow*, double, double);
	// GLFW callbacks
	void glfw_callback_key_press(GLFWwindow*, int, int, int, int);

	//render pass
	lut::RenderPass create_render_pass(lut::VulkanWindow const&);
	lut::RenderPass create_render_pass_gbuffer(lut::VulkanWindow const&);
	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const&);
	lut::DescriptorSetLayout create_object_descriptor_layout(lut::VulkanWindow const&);
	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const&
		, VkDescriptorSetLayout aSceneDescriptorLayout
		, VkDescriptorSetLayout aObjectDescriptorLayout);
	lut::Pipeline create_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	lut::Pipeline create_gbuffer_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);

	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const&, lut::Allocator const&);
	std::tuple<lut::Image, lut::ImageView> create_gbuffer(lut::VulkanWindow const&, lut::Allocator const&, VkFormat, VkImageUsageFlags);
	lut::PipelineLayout create_postprocessing_pipeline_layout(lut::VulkanContext const& , VkDescriptorSetLayout, VkDescriptorSetLayout );
	lut::Pipeline create_postprocessing_pipeline(lut::VulkanWindow const& , VkRenderPass , VkPipelineLayout );
	lut::PipelineLayout create_postprocessing_pipeline_layout(lut::VulkanContext const&, VkDescriptorSetLayout);
	void create_postprocessing_framebuffers(lut::VulkanWindow const& , VkRenderPass , std::vector<lut::Framebuffer>& , std::vector<VkImageView>& );
	lut::DescriptorSetLayout create_postprocessing_descriptor_layout(lut::VulkanWindow const& );
	lut::RenderPass create_postprocessing_render_pass(lut::VulkanWindow const& );

	void create_swapchain_framebuffers(
		lut::VulkanWindow const&,
		VkRenderPass,
		std::vector<lut::Framebuffer>&,
		VkImageView aDepthView
	);

	void create_gbuffer_framebuffers(lut::VulkanWindow const&, 
		VkRenderPass, 
		lut::Framebuffer&, 
		std::vector<VkImageView>);

	void update_scene_uniforms(
		glsl::SceneUniform&,
		std::uint32_t aFramebufferWidth,
		std::uint32_t aFramebufferHeight,
		UserState const&
	);

	void update_user_state(UserState&, float aElapsedTime);

	void record_commands(
		VkCommandBuffer,
		VkCommandBuffer,
		VkRenderPass,
		VkRenderPass,
		VkFramebuffer,
		VkFramebuffer,
		VkPipeline,
		VkPipeline,
		VkExtent2D const&,
		VkBuffer aPositionBuffer,
		VkBuffer aTexCoordBuffer,
		VkBuffer normalBuffer,
		std::uint32_t aVertexCount,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const&,
		VkPipelineLayout,
		VkPipelineLayout,
		VkDescriptorSet aSceneDescriptors,
		VkDescriptorSet ,
		//VkDescriptorSet aObjectDescriptors,
		std::vector<TMesh> const& aModleMesh,
		std::vector<VkDescriptorSet> const& aDescriptorA
	);
	void submit_commands(
		lut::VulkanWindow const&,
		VkCommandBuffer,
		VkCommandBuffer,
		VkFence,
		VkFence,
		VkSemaphore,
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

	void begin_renderapss(VkCommandBuffer, VkRenderPass, VkFramebuffer, VkExtent2D);

	void draw_graphics_pipeline(VkCommandBuffer, VkPipeline,
								VkPipelineLayout, VkDescriptorSet, 
								std::vector<TMesh> const& , std::vector<VkDescriptorSet> const&);

}

int main() try
{
	//TODO-implement me.
	auto window = lut::make_vulkan_window();
	//glfw callback
	UserState state{};
	glfwSetWindowUserPointer(window.window, &state);
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);
	glfwSetMouseButtonCallback(window.window, &glfw_callback_button);
	glfwSetCursorPosCallback(window.window, &glfw_callback_motion);
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);

	//-----render pass------
	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator(window);
	// Intialize resources
	//lut::RenderPass renderPass = create_render_pass(window);
	lut::DescriptorSetLayout sceneLayout = create_scene_descriptor_layout(window);
	lut::DescriptorSetLayout objectLayout = create_object_descriptor_layout(window);
	lut::PipelineLayout pipeLayout = create_pipeline_layout(window, sceneLayout.handle, objectLayout.handle);
	//lut::Pipeline pipe = create_pipeline(window, renderPass.handle, pipeLayout.handle);

	
	lut::Framebuffer gbuffer_framebuffer;

	//---------create depth buffer and command pool and framebuffer
	auto [depthBuffer, depthBufferView] = create_depth_buffer(window, allocator);
	std::vector<lut::Framebuffer> framebuffers;

	// initial gBuffer
	lut::RenderPass GbufferRenderPass = create_render_pass_gbuffer(window);
	// create gBuffer pipeline
	lut::Pipeline Gbufferpipe = create_gbuffer_pipeline(window, GbufferRenderPass.handle, pipeLayout.handle);
	// create gbuffer
	VkImageUsageFlags flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	auto [vertexBuffer, vertexBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
	auto [normalBuffer, normalBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
	auto [baseColorBuffer, baseColorBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
	auto [mtlBuffer, mtlBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
	auto [roughnessBuffer, roughnessBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
	std::vector<VkImageView> gbufferAttachments = {
		vertexBufferView.handle,
		normalBufferView.handle,
		baseColorBufferView.handle,
		mtlBufferView.handle,
		roughnessBufferView.handle
	};
	gbufferAttachments.push_back(depthBufferView.handle);
	create_gbuffer_framebuffers(window, GbufferRenderPass.handle, gbuffer_framebuffer, gbufferAttachments);


	// initial postprocessing
	lut::RenderPass postprocessingRenderPass = create_postprocessing_render_pass(window);
	lut::DescriptorSetLayout postprocessingLayout = create_postprocessing_descriptor_layout(window);
	lut::PipelineLayout  postprocessingPipeLayout = create_postprocessing_pipeline_layout(window, sceneLayout.handle, postprocessingLayout.handle);
	lut::Pipeline postprocessingPipe = create_postprocessing_pipeline(window, postprocessingRenderPass.handle, postprocessingPipeLayout.handle);
	create_postprocessing_framebuffers(window, postprocessingRenderPass.handle, framebuffers, gbufferAttachments);

	lut::CommandPool cpool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> cbuffers;
	std::vector<VkCommandBuffer> cbuffersB;
	std::vector<lut::Fence> cbfences;
	std::vector<lut::Fence> cbfencesB;

	//for (std::size_t i = 0; i < framebuffers.size(); ++i)
	for (std::size_t i = 0; i < framebuffers.size(); ++i)
	{
		cbuffers.emplace_back(lut::alloc_command_buffer(window, cpool.handle));
		cbuffersB.emplace_back(lut::alloc_command_buffer(window, cpool.handle));
		cbfences.emplace_back(lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT));
		cbfencesB.emplace_back(lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT));
	}

	lut::Semaphore imageAvailable = lut::create_semaphore(window);
	lut::Semaphore renderFinished = lut::create_semaphore(window);
	lut::Semaphore renderFinishedB = lut::create_semaphore(window);


	//test5---------------load obj
	BakedModel bakedObject = load_baked_model("assets/cw3/suntemple.comp5822mesh");
	
	TMesh bakedMesh;

	std::vector<TMesh> modelMesh;
	
	for (auto const& mesh : bakedObject.meshes)
	{

		std::vector<glm::vec3> positions = mesh.positions;
		std::vector<glm::vec2> texcoords = mesh.texcoords;
		std::vector<glm::vec3> normals = mesh.normals;

		std::vector<std::uint32_t> indices = mesh.indices;
		std::uint32_t indexCount = static_cast<std::uint32_t>(indices.size());
		//std::cout << "tex size " << texcoords.size() << " Normal size " << normals.size() << " tangent size " << tangents.size() << std::endl;
		bakedMesh = split_meshes(window, allocator, positions, texcoords, normals, indices);
		//bakedMesh = split_meshes(window, allocator, positions, texcoords, normals,  indices);

		bakedMesh.indexCount = indexCount;
		modelMesh.push_back(std::move(bakedMesh));
		
	}


	lut::Buffer sceneUBO = lut::create_buffer(
		allocator,
		sizeof(glsl::SceneUniform),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);

	//TODO- (Section 3) create descriptor pool
	lut::DescriptorPool dpool = lut::create_descriptor_pool(window);
	//TODO- (Section 3) allocate descriptor set for uniform buffer
	//TODO- (Section 3) initialize descriptor set with vkUpdateDescriptorSets
	VkDescriptorSet sceneDescriptors = lut::alloc_desc_set(window, dpool.handle, sceneLayout.handle);
	{
		VkWriteDescriptorSet desc[1]{};
		VkDescriptorBufferInfo sceneUboInfo{};
		sceneUboInfo.buffer = sceneUBO.buffer;
		sceneUboInfo.range = VK_WHOLE_SIZE;
		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = sceneDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		desc[0].descriptorCount = 1;
		desc[0].pBufferInfo = &sceneUboInfo;
		/* write in descriptor set */
		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}

	// descriptor of basecolor
	std::vector<VkDescriptorSet> descriptors;
	std::vector<lut::ImageView> texViews;
	lut::Sampler texSamples = lut::create_default_sampler(window, false);
	std::vector<lut::Image> texImages;
	lut::CommandPool loadCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// descriptor of material color
	std::vector<VkDescriptorSet> mtlDes;
	std::vector<lut::ImageView> mtlViews;
	lut::Sampler mtlSamples = lut::create_default_sampler(window, false);
	std::vector<lut::Image> mtlImages;
	lut::CommandPool loadCmdPool_mtl = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// descriptor of roughtness color
	std::vector<VkDescriptorSet> rogDes;
	std::vector<lut::ImageView> rogViews;
	lut::Sampler rogSamples = lut::create_default_sampler(window, false);
	std::vector<lut::Image> rogImages;
	lut::CommandPool loadCmdPool_rog = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
		
	// descriptor of normalmap
	std::vector<VkDescriptorSet> norDes;
	std::vector<lut::ImageView> norViews;
	lut::Sampler norSamples = lut::create_default_sampler(window, false);
	std::vector<lut::Image> norImages;
	lut::CommandPool loadCmdPool_nor = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	

	
	std::vector<std::string> newTexPath;
	for (size_t i = 0; i < bakedObject.textures.size() ; i++) {
		if (i == 4 || i == 9)
			continue;
		newTexPath.push_back(bakedObject.textures[i].path);
	}

	//for (auto& s : newTexPath) {
	//	std::cout << s.c_str() << std::endl;
	//}

	//for (size_t i = 0; i < (bakedObject.textures.size() - 2)/4; i++) {
	//	std::cout << i << "base --" << bakedObject.materials[i].baseColorTextureId <<"---" << bakedObject.textures[i].path << std::endl;
	//	std::cout << i << "rog --" << bakedObject.materials[i].roughnessTextureId << std::endl;
	//	std::cout << i << "metl --" << bakedObject.materials[i].metalnessTextureId << std::endl;
	//	std::cout << i << "nor --" << bakedObject.materials[i].normalMapTextureId << std::endl;
	//	std::cout << i << "emis --" << bakedObject.materials[i].emissiveTextureId << std::endl;
	//	std::cout << "------" << std::endl;
	//}

		for (std::size_t i = 0, p=0; i < bakedObject.textures.size()-2; i+=4,p++)
		{
			
			//std::cout << bakedObject.meshes[p].materialId << std::endl;
			auto materialId = bakedObject.meshes[p].materialId * 4;
			VkDescriptorSet ds = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			std::string texturePath = newTexPath[materialId];

			texImages.push_back(lut::load_image_texture2d(texturePath.c_str(), window, loadCmdPool.handle, allocator));
			texViews.push_back(lut::create_image_view_texture2d(window, texImages[p].image, VK_FORMAT_R8G8B8A8_SRGB));

			VkWriteDescriptorSet desc[3]{};
			VkDescriptorImageInfo textureInfo{};
			textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			textureInfo.imageView = texViews[p].handle;
			textureInfo.sampler = texSamples.handle;
			desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[0].dstSet = ds;
			desc[0].dstBinding = 0;
			desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[0].descriptorCount = 1;
			desc[0].pImageInfo = &textureInfo;
			
			// ------------------- des of mtl
			std::string mtlTexturePath = newTexPath[materialId+2];

			mtlImages.push_back(lut::load_image_texture2d(mtlTexturePath.c_str(), window, loadCmdPool_mtl.handle, allocator));
			mtlViews.push_back(lut::create_image_view_texture2d(window, mtlImages[p].image, VK_FORMAT_R8G8B8A8_SRGB));

			VkDescriptorImageInfo mtlTexInfo{};
			mtlTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mtlTexInfo.imageView = mtlViews[p].handle;
			mtlTexInfo.sampler = mtlSamples.handle;
			desc[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[1].dstSet = ds;
			desc[1].dstBinding = 1;
			desc[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[1].descriptorCount = 1;
			desc[1].pImageInfo = &mtlTexInfo;

			// -------------- des of roughness 
			std::string rogTexturePath = newTexPath[materialId+1];

			rogImages.push_back(lut::load_image_texture2d(rogTexturePath.c_str(), window, loadCmdPool_rog.handle, allocator));
			rogViews.push_back(lut::create_image_view_texture2d(window, rogImages[p].image, VK_FORMAT_R8G8B8A8_SRGB));

			VkDescriptorImageInfo rogTexInfo{};
			rogTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rogTexInfo.imageView = rogViews[p].handle;
			rogTexInfo.sampler = rogSamples.handle;
			desc[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[2].dstSet = ds;
			desc[2].dstBinding = 2;
			desc[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[2].descriptorCount = 1;
			desc[2].pImageInfo = &rogTexInfo;

			vkUpdateDescriptorSets(window.device, 3, desc, 0, nullptr);
			descriptors.push_back(std::move(ds));
		}
		std::cout << " ---------------------------" << std::endl;

		lut::DescriptorPool postDpool = lut::create_descriptor_pool(window);
		VkDescriptorSet postDescriptors = lut::alloc_desc_set(window, postDpool.handle, postprocessingLayout.handle);
		lut::Sampler gBufferSampler = lut::create_default_sampler(window, false);
		std::vector<VkWriteDescriptorSet> descriptorWrites(gbufferAttachments.size()-1);
		std::vector<VkDescriptorImageInfo> imageInfos(gbufferAttachments.size()-1);
		{
			for (size_t i = 0; i < gbufferAttachments.size()-1; ++i) {
				imageInfos[i].imageView = gbufferAttachments[i];
				imageInfos[i].sampler = gBufferSampler.handle;
				imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				descriptorWrites[i].dstSet = postDescriptors;
				descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				descriptorWrites[i].dstBinding = i; 
				descriptorWrites[i].pImageInfo = &imageInfos[i];
				descriptorWrites[i].descriptorCount = 1;
			}
			
			vkUpdateDescriptorSets(window.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
		}
	//--------END--------------------------------

	//Application main loop
	bool recreateSwapchain = false;
	auto previousClock = Clock_::now();
	while (!glfwWindowShouldClose(window.window)) {
		glfwPollEvents();

		if (recreateSwapchain)
		{
			//TODO: (Exercise 1.4) re-create swapchain and associated resources - see Exercise 3!
			vkDeviceWaitIdle(window.device);
			//recreate them
			auto const changes = recreate_swapchain(window);
			if (changes.changedFormat) {
				GbufferRenderPass = create_render_pass_gbuffer(window);
				postprocessingRenderPass = create_postprocessing_render_pass(window);
			}
			if (changes.changedSize){
				std::tie(depthBuffer, depthBufferView) = create_depth_buffer(window, allocator);
				//VkImageUsageFlags flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				//auto [vertexBuffer, vertexBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
				//auto [normalBuffer, normalBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
				//auto [baseColorBuffer, baseColorBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
				//auto [mtlBuffer, mtlBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
				//auto [roughnessBuffer, roughnessBufferView] = create_gbuffer(window, allocator, VK_FORMAT_R16G16B16A16_SFLOAT, flags);
				//std::vector<VkImageView> gbufferAttachments = {
				//	vertexBufferView.handle,
				//	normalBufferView.handle,
				//	baseColorBufferView.handle,
				//	mtlBufferView.handle,
				//	roughnessBufferView.handle
				//};
				//gbufferAttachments.push_back(depthBufferView.handle);
			}

			framebuffers.clear();
			create_gbuffer_framebuffers(window, GbufferRenderPass.handle, gbuffer_framebuffer, gbufferAttachments);
			create_postprocessing_framebuffers(window, postprocessingRenderPass.handle, framebuffers, gbufferAttachments);
			if (changes.changedSize)
			{
				Gbufferpipe = create_gbuffer_pipeline(window, GbufferRenderPass.handle, pipeLayout.handle);
				postprocessingPipe = create_postprocessing_pipeline(window, postprocessingRenderPass.handle, postprocessingPipeLayout.handle);

			}
			recreateSwapchain = false;
			continue;

		}

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
		if (auto const res = vkWaitForFences(window.device, 1, &cbfences[imageIndex].handle, VK_TRUE, std::numeric_limits<std::uint64_t>::max()); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to wait for command buffer fence %u\n"
				"vkWaitForFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}
		if (auto const res = vkResetFences(window.device, 1, &cbfences[imageIndex].handle); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to reset command buffer fence %u\n"
				"vkResetFences() returned %s", imageIndex, lut::to_string(res).c_str());
		}

		auto const now = Clock_::now();
		auto const dt = std::chrono::duration_cast<Secondsf_>(now - previousClock).count();
		previousClock = now;
		update_user_state(state, dt);

		//TODO: record and submit commands
		assert(std::size_t(imageIndex) < cbuffers.size());
		assert(std::size_t(imageIndex) < framebuffers.size());

		// Prepare data for this frame
		glsl::SceneUniform sceneUniforms{};
		update_scene_uniforms(sceneUniforms, window.swapchainExtent.width, window.swapchainExtent.height, state);

		record_commands(
			cbuffers[imageIndex],
			cbuffersB[imageIndex],
			GbufferRenderPass.handle,
			postprocessingRenderPass.handle,
			gbuffer_framebuffer.handle,
			framebuffers[imageIndex].handle,
			Gbufferpipe.handle,
			postprocessingPipe.handle,
			window.swapchainExtent,
			bakedMesh.positions.buffer,
			bakedMesh.texcoords.buffer,
			bakedMesh.normals.buffer,
			bakedMesh.indexCount,
			sceneUBO.buffer,
			sceneUniforms,
			pipeLayout.handle,
			postprocessingPipeLayout.handle,
			sceneDescriptors,
			postDescriptors,
			//floorDescriptors,
			modelMesh,
			descriptors
		);

		submit_commands(
			window,
			cbuffers[imageIndex],
			cbuffersB[imageIndex],
			cbfences[imageIndex].handle,
			cbfencesB[imageIndex].handle,
			imageAvailable.handle,
			renderFinished.handle,
			renderFinishedB.handle
		);

		present_results(window.presentQueue, window.swapchain, imageIndex, renderFinishedB.handle, recreateSwapchain);

	}
	vkDeviceWaitIdle(window.device);
	return 0;
}
catch( std::exception const& eErr )
{
	std::fprintf( stderr, "\n" );
	std::fprintf( stderr, "Error: %s\n", eErr.what() );
	return 1;
}

//glfw callback
namespace {
	void glfw_callback_key_press(GLFWwindow* aWindow, int aKey, int /*aScanCode*/, int aAction, int /*aModifierFlags*/)
	{
		if (GLFW_KEY_ESCAPE == aKey && GLFW_PRESS == aAction)
		{
			glfwSetWindowShouldClose(aWindow, GLFW_TRUE);
		}
		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWindow));
		assert(state);
		const bool isReleased = (GLFW_RELEASE == aAction);
		switch (aKey)
		{
		case GLFW_KEY_W:
			state->inputMap[std::size_t(EInputState::forward)] = !isReleased;
			break;
		case GLFW_KEY_S:
			state->inputMap[std::size_t(EInputState::backward)] = !isReleased;
			break;
		case GLFW_KEY_A:
			state->inputMap[std::size_t(EInputState::strafeLeft)] = !isReleased;
			break;
		case GLFW_KEY_D:
			state->inputMap[std::size_t(EInputState::strafeRight)] = !isReleased;
			break;
		case GLFW_KEY_E:
			state->inputMap[std::size_t(EInputState::levitate)] = !isReleased;
			break;
		case GLFW_KEY_Q:
			state->inputMap[std::size_t(EInputState::sink)] = !isReleased;
			break;
		case GLFW_KEY_LEFT_SHIFT:
			[[fallthrough]];
		case GLFW_KEY_RIGHT_SHIFT:
			state->inputMap[std::size_t(EInputState::fast)] = !isReleased;
			break;
		case GLFW_KEY_LEFT_CONTROL:
			[[fallthrough]];
		case GLFW_KEY_RIGHT_CONTROL:
			state->inputMap[std::size_t(EInputState::slow)] = !isReleased;
			break;
		default:
			;
		}
	}
	void glfw_callback_button(GLFWwindow* aWin, int aBut, int aAct, int)
	{
		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWin));
		assert(state);
		if (GLFW_MOUSE_BUTTON_RIGHT == aBut && GLFW_PRESS == aAct)
		{
			auto& flag = state->inputMap[std::size_t(EInputState::mousing)];
			flag = !flag;
			if (flag)
				glfwSetInputMode(aWin, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			else
				glfwSetInputMode(aWin, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
	void glfw_callback_motion(GLFWwindow* aWin, double aX, double aY)
	{
		auto state = static_cast<UserState*>(glfwGetWindowUserPointer(aWin));
		assert(state);
		state->mouseX = float(aX);
		state->mouseY = float(aY);
	}

	void update_user_state(UserState& aState, float aElapsedTime)
	{
		auto& cam = aState.camera2world;
		if (aState.inputMap[std::size_t(EInputState::mousing)])
		{
			if (aState.wasMousing)
			{
				auto const sens = cfg::kCameraMouseSensitivity;
				auto const dx = sens * (aState.mouseX - aState.previousX);
				auto const dy = sens * (aState.mouseY - aState.previousY);
				cam = cam * glm::rotate(-dy, glm::vec3(1.f, 0.f, 0.f));
				cam = cam * glm::rotate(-dx, glm::vec3(0.f, 1.f, 0.f));
			}
			aState.previousX = aState.mouseX;
			aState.previousY = aState.mouseY;
			aState.wasMousing = true;
		}
		else
		{
			aState.wasMousing = false;
		}
		auto const move = aElapsedTime * cfg::kCameraBaseSpeed *
			(aState.inputMap[std::size_t(EInputState::fast)] ? cfg::kCameraFastMult : 1.f) *
			(aState.inputMap[std::size_t(EInputState::slow)] ? cfg::kCameraSlowMult : 1.f);
		if (aState.inputMap[std::size_t(EInputState::forward)])
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, -move));
		if (aState.inputMap[std::size_t(EInputState::backward)])
			cam = cam * glm::translate(glm::vec3(0.f, 0.f, +move));
		if (aState.inputMap[std::size_t(EInputState::strafeLeft)])
			cam = cam * glm::translate(glm::vec3(-move, 0.f, 0.f));
		if (aState.inputMap[std::size_t(EInputState::strafeRight)])
			cam = cam * glm::translate(glm::vec3(+move, 0.f, 0.f));
		if (aState.inputMap[std::size_t(EInputState::levitate)])
			cam = cam * glm::translate(glm::vec3(0.f, +move, 0.f));
		if (aState.inputMap[std::size_t(EInputState::sink)])
			cam = cam * glm::translate(glm::vec3(0.f, -move, 0.f));
	}
}

//rennder pass
namespace {
	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 3) implement me!
		VkDescriptorSetLayoutBinding bindings[1]{};
		// Input vertex buffer
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;

		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::DescriptorSetLayout(aWindow.device, layout);
	}
	lut::DescriptorSetLayout create_object_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: (Section 4) implement me!
		VkDescriptorSetLayoutBinding bindings[3]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[2].binding = 2;
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 3;// sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::DescriptorSetLayout(aWindow.device, layout);
	}
	lut::RenderPass create_render_pass(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
		VkAttachmentDescription attachments[2]{};
		attachments[0].format = aWindow.swapchainFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		attachments[1].format = cfg::kDepthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkAttachmentReference subpassAttachments[1]{};
		subpassAttachments[0].attachment = 0;
		subpassAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachment{};
		depthAttachment.attachment = 1;
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1;
		subpasses[0].pColorAttachments = subpassAttachments;
		subpasses[0].pDepthStencilAttachment = &depthAttachment;

		VkSubpassDependency deps[2]{};
		deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].srcAccessMask = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].dstSubpass = 0;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[1].dstSubpass = 0;
		deps[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 2;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 2;
		passInfo.pDependencies = deps;
		VkRenderPass rpass = VK_NULL_HANDLE;
		if (auto const res = vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &rpass); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create render pass\n"
				"vkCreateRenderPass() returned %s", lut::to_string(res).c_str());
		}
		return lut::RenderPass(aWindow.device, rpass);
	}

	lut::RenderPass create_render_pass_gbuffer(lut::VulkanWindow const& aWindow) {
		VkAttachmentDescription attachments[6]{};
		VkAttachmentReference subpassAttachments[5]{};
		for (int i = 0; i < 5; i++) {
			attachments[i].format = VK_FORMAT_R16G16B16A16_SFLOAT;
			attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
			attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			subpassAttachments[i].attachment = i;
			subpassAttachments[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		////normal att
		//attachments[1].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		//attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		//attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		//attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		//attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		//attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		//// basecolor att
		//attachments[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		//attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
		//attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		//attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		//attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		//attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments[5].format = cfg::kDepthFormat;
		attachments[5].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[5].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[5].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[5].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[5].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		//vertex
		////normal
		//subpassAttachments[1].attachment = 1;
		//subpassAttachments[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		////rog
		//subpassAttachments[2].attachment = 2;
		//subpassAttachments[2].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachment{};
		depthAttachment.attachment = 5;
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDependency deps[6]{};
		//vertex
		for (int i = 0; i < 5; i++) {
			deps[i].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
			deps[i].srcSubpass = VK_SUBPASS_EXTERNAL;
			deps[i].srcAccessMask = 0;
			deps[i].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			deps[i].dstSubpass = 0;
			deps[i].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			deps[i].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		////normal
		//deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		//deps[1].srcSubpass = VK_SUBPASS_EXTERNAL;
		//deps[1].srcAccessMask = 0;
		//deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		//deps[1].dstSubpass = 0;
		//deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		//deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		////rog
		//deps[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		//deps[2].srcSubpass = VK_SUBPASS_EXTERNAL;
		//deps[2].srcAccessMask = 0;
		//deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		//deps[2].dstSubpass = 0;
		//deps[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		//deps[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		//depth
		deps[5].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[5].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[5].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[5].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		deps[5].dstSubpass = 0;
		deps[5].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		deps[5].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 5;
		subpasses[0].pColorAttachments = subpassAttachments;
		subpasses[0].pDepthStencilAttachment = &depthAttachment;

		VkRenderPassCreateInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		passInfo.attachmentCount = 6;
		passInfo.pAttachments = attachments;
		passInfo.subpassCount = 1;
		passInfo.pSubpasses = subpasses;
		passInfo.dependencyCount = 6;
		passInfo.pDependencies = deps;

		VkRenderPass renderPass;
		if (vkCreateRenderPass(aWindow.device, &passInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}
		return lut::RenderPass(aWindow.device, renderPass);
	}

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
		VkDescriptorSetLayout layouts[] = {
			// Order must match the set = N in the shaders
			aSceneLayout, // set 0
			aObjectLayout  // set 1
		};
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;// sizeof(layouts) / sizeof(layouts[0]);
		layoutInfo.pSetLayouts = layouts;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create pipeline layout\n" "vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::PipelineLayout(aContext.device, layout);
	}

	lut::Pipeline create_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout){
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);



		//lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kMipmapShaderPath);
		// Define shader stages in the pipeline
		// The code for Mesh Density
		VkPipelineShaderStageCreateInfo stages[1]{};
		// End
		//VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;

		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription vertexInputs[3]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 2;
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[2].binding = 2;
		vertexInputs[2].stride = sizeof(float) * 3;
		vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;


		VkVertexInputAttributeDescription vertexAttributes[3]{};
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].location = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[0].offset = 0;

		vertexAttributes[1].binding = 1;
		vertexAttributes[1].location = 1;
		vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[1].offset = 0;

		vertexAttributes[2].binding = 2; 
		vertexAttributes[2].location = 2; 
		vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT; 
		vertexAttributes[2].offset = 0;


		inputInfo.vertexBindingDescriptionCount = 3;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 3;
		inputInfo.pVertexAttributeDescriptions = vertexAttributes;
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
		//rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.cullMode = VK_CULL_MODE_NONE;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f;
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

		// -------END ----------


		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;

		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

		pipeInfo.stageCount = 2;
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr;
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = &depthInfo;
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr;
		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0;
		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}
		return lut::Pipeline(aWindow.device, pipe);
	}

	lut::Pipeline create_gbuffer_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout) {
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);

		// The code for Mesh Density
		//VkPipelineShaderStageCreateInfo stages[1]{};
		// End
		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;

		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription vertexInputs[3]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 2;
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[2].binding = 2;
		vertexInputs[2].stride = sizeof(float) * 3;
		vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;


		VkVertexInputAttributeDescription vertexAttributes[3]{};
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].location = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[0].offset = 0;

		vertexAttributes[1].binding = 1;
		vertexAttributes[1].location = 1;
		vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[1].offset = 0;

		vertexAttributes[2].binding = 2;
		vertexAttributes[2].location = 2;
		vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[2].offset = 0;


		inputInfo.vertexBindingDescriptionCount = 3;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 3;
		inputInfo.pVertexAttributeDescriptions = vertexAttributes;
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
		//rasterInfo.cullMode = VK_CULL_MODE_NONE;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f;
		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		// Define blend state
		// We define one blend state per color attachment - this example uses a single color attachment, so we only
		// need one. Right now, we dont do any blending, so we can ignore most of the members.
		VkPipelineColorBlendAttachmentState blendStates[5]{};
		for (int i = 0; i <5; i++) {
			blendStates[i].blendEnable = VK_FALSE;
			blendStates[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		}

		// -------END ----------


		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 5;
		blendInfo.pAttachments = blendStates;

		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount = 2;
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr;
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = &depthInfo;
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDynamicState = nullptr;
		pipeInfo.layout = aPipelineLayout;
		pipeInfo.renderPass = aRenderPass;
		pipeInfo.subpass = 0;
		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}
		return lut::Pipeline(aWindow.device, pipe);
	}

	void create_gbuffer_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, lut::Framebuffer& aFramebuffers, std::vector<VkImageView> attachments) {
		//assert(aFramebuffers.empty());

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.flags = 0;
		fbInfo.renderPass = aRenderPass;
		fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbInfo.pAttachments = attachments.data();
		fbInfo.width = aWindow.swapchainExtent.width;
		fbInfo.height = aWindow.swapchainExtent.height;
		fbInfo.layers = 1;

		VkFramebuffer fb = VK_NULL_HANDLE;
		if (auto const res = vkCreateFramebuffer(aWindow.device, &fbInfo, nullptr, &fb); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create framebuffer for swap chain image\n"
				"vkCreateFramebuffer() returned %s", lut::to_string(res).c_str());
		}
		aFramebuffers = lut::Framebuffer(aWindow.device, fb);
		//assert(aWindow.swapViews.size() == aFramebuffers.size());
	}

	//postprocessing setting
	lut::DescriptorSetLayout create_postprocessing_descriptor_layout(lut::VulkanWindow const& aWindow) {
		VkDescriptorSetLayoutBinding bindings[5]{};
		for (int i = 0; i < 5; i++) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		//bindings[0].binding = 0;
		//bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		//bindings[0].descriptorCount = 1;
		//bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::DescriptorSetLayout(aWindow.device, layout);
	}

	lut::PipelineLayout create_postprocessing_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout,VkDescriptorSetLayout aPostProcessingLayout) {
		VkDescriptorSetLayout layouts[] = {
			// Order must match the set = N in the shaders
			aSceneLayout,
			aPostProcessingLayout
		};
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount =  sizeof(layouts) / sizeof(layouts[0]);
		layoutInfo.pSetLayouts = layouts;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (auto const res = vkCreatePipelineLayout(aContext.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create pipeline layout\n" "vkCreatePipelineLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::PipelineLayout(aContext.device, layout);
	}

	lut::RenderPass create_postprocessing_render_pass(lut::VulkanWindow const& aWindow) {
		
		VkAttachmentDescription attachments[2]{};
		attachments[0].format = aWindow.swapchainFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;


		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		VkSubpassDependency dependencies[1]{};
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = 0;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = attachments;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = dependencies;

		VkRenderPass renderPass;
		if (vkCreateRenderPass(aWindow.device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create postprocessing render pass!");
		}

		return lut::RenderPass(aWindow.device, renderPass);
	}

	lut::Pipeline create_postprocessing_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout) {
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::sVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::sFragShaderPath);

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";

		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthInfo.depthTestEnable = VK_FALSE; 
		depthInfo.depthWriteEnable = VK_FALSE; 
		depthInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS; 

		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		inputInfo.vertexBindingDescriptionCount = 0; // No vertex bindings
		inputInfo.pVertexBindingDescriptions = nullptr; // No vertex bindings
		inputInfo.vertexAttributeDescriptionCount = 0; // No vertex attributes
		inputInfo.pVertexAttributeDescriptions = nullptr; // No vertex attributes

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
		//rasterInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterInfo.cullMode = VK_CULL_MODE_NONE;
		rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterInfo.depthBiasEnable = VK_FALSE;
		rasterInfo.lineWidth = 1.f;
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

		// -------END ----------


		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;

		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeInfo.stageCount = 2; // Vertex and Fragment shaders
		pipeInfo.pStages = stages; // Attach shader stages
		pipeInfo.pVertexInputState = &inputInfo; // Vertex input (none needed)
		pipeInfo.pInputAssemblyState = &assemblyInfo; // Input assembly (triangles)
		pipeInfo.pViewportState = &viewportInfo; // Viewport and scissor
		pipeInfo.pRasterizationState = &rasterInfo; // Rasterization settings
		pipeInfo.pMultisampleState = &samplingInfo; // Multisampling (none)
		pipeInfo.pDepthStencilState = &depthInfo; // No depth or stencil needed for post-processing
		pipeInfo.pColorBlendState = &blendInfo; // Color blending settings
		pipeInfo.pDynamicState = nullptr; // No dynamic states
		pipeInfo.layout = aPipelineLayout; // Pipeline layout
		pipeInfo.renderPass = aRenderPass; // Render pass
		pipeInfo.subpass = 0; // Index of the subpass where this pipeline will be used

		VkPipeline pipe = VK_NULL_HANDLE;
		if (auto const res = vkCreateGraphicsPipelines(aWindow.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipe); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create graphics pipeline\n" "vkCreateGraphicsPipelines() returned %s", lut::to_string(res).c_str());
		}
		return lut::Pipeline(aWindow.device, pipe);
	}

	void create_postprocessing_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers, std::vector<VkImageView>& aImageView) {
		assert(aFramebuffers.empty());
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		for (std::size_t i = 0; i < aWindow.swapViews.size(); ++i)
		{
			VkImageView attachments[1] = {
				aWindow.swapViews[i],
			};

			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0;
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
		assert(aWindow.swapViews.size() == aFramebuffers.size());
	}

	void record_commands(
		VkCommandBuffer aCmdBuff,
		VkCommandBuffer aCmdBuffB,
		VkRenderPass aRenderPassA,
		VkRenderPass aRenderPassB,
		VkFramebuffer aFramebufferA,
		VkFramebuffer aFramebufferB,
		VkPipeline aGraphicsPipeA,
		VkPipeline aGraphicsPipeB,
		VkExtent2D const& aImageExtent,
		VkBuffer aPositionBuffer,
		VkBuffer TexCoordBuffer,
		VkBuffer normalBuffer,
		std::uint32_t aVertexCount,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const& aSceneUniform,
		VkPipelineLayout aGraphicsLayout,
		VkPipelineLayout aPostprocessingLayout,
		VkDescriptorSet aSceneDescriptors,
		VkDescriptorSet aPostDescriptors,
		//VkDescriptorSet aObjectDecriptors,
		std::vector<TMesh> const& aModelMesh,
		std::vector<VkDescriptorSet> const& aDescriptorA
	)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT;
		begInfo.pInheritanceInfo = nullptr;
		if (auto const res = vkBeginCommandBuffer(aCmdBuff, &begInfo); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to begin recording command buffer\n" "vkBeginCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
		// Upload scene uniforms
		lut::buffer_barrier(aCmdBuff,
			aSceneUBO,
			VK_ACCESS_UNIFORM_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT
		);
		vkCmdUpdateBuffer(aCmdBuff, aSceneUBO, 0, sizeof(glsl::SceneUniform), &aSceneUniform);
		lut::buffer_barrier(aCmdBuff,
			aSceneUBO,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_UNIFORM_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
		);

		// records and draw renderPass A
		// Begin render pass
		VkClearValue clearValues[6]{};
		for (int i = 0; i < 5; i++) {
			clearValues[i].color.float32[0] = 0.1f; // Clear to a dark gray background.
			clearValues[i].color.float32[1] = 0.1f; // If we were debugging, this would potentially
			clearValues[i].color.float32[2] = 0.1f; // help us see whether the render pass took
			clearValues[i].color.float32[3] = 1.f; // place, even if nothing else was drawn.
		}
		clearValues[5].depthStencil.depth = 1.0f;
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPassA;
		passInfo.framebuffer = aFramebufferA;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 6;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		// Begin drawing with our graphics pipeline
		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeA);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		for (size_t i = 0; i < aModelMesh.size(); i++)
		{
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aDescriptorA[i], 0, nullptr);

			VkBuffer buffers[3] = { aModelMesh[i].positions.buffer, aModelMesh[i].texcoords.buffer, aModelMesh[i].normals.buffer };
			VkDeviceSize offsets[4]{};
			vkCmdBindVertexBuffers(aCmdBuff, 0,3, buffers, offsets);
			vkCmdBindIndexBuffer(aCmdBuff, aModelMesh[i].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, aModelMesh[i].indexCount, 1, 0, 0, 0);
		}
		// End the render pass
		vkCmdEndRenderPass(aCmdBuff);
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}

		// insert barrier
		VkCommandBufferBeginInfo begInfoB = {};
		begInfoB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfoB.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		if (vkBeginCommandBuffer(aCmdBuffB, &begInfoB) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin recording command buffer for Render Pass B.");
		}
		//VkMemoryBarrier barrier{};
		//barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		//barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		//barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		//vkCmdPipelineBarrier(
		//	aCmdBuffB,
		//	VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		//	0,
		//	1, &barrier,
		//	0, nullptr,
		//	0, nullptr
		//);
		
		// records and draw renderPass B
		VkClearValue clearValuesB[1]{};
		for (auto& c : clearValuesB) {
			c.color.float32[0] = 0.1f; // Clear to a dark gray background.
			c.color.float32[1] = 0.1f; // If we were debugging, this would potentially
			c.color.float32[2] = 0.1f; // help us see whether the render pass took
			c.color.float32[3] = 1.f; // place, even if nothing else was drawn.
		}
		VkRenderPassBeginInfo passInfoB{};
		passInfoB.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfoB.renderPass = aRenderPassB;
		passInfoB.framebuffer = aFramebufferB;
		passInfoB.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfoB.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfoB.clearValueCount = 1;
		passInfoB.pClearValues = clearValuesB;
		vkCmdBeginRenderPass(aCmdBuffB, &passInfoB, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(aCmdBuffB, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeB);
		//vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
		vkCmdBindDescriptorSets(aCmdBuffB, VK_PIPELINE_BIND_POINT_GRAPHICS, aPostprocessingLayout, 1, 1, &aPostDescriptors, 0, nullptr);
		vkCmdDraw(aCmdBuffB, 3, 1, 0, 0);
		vkCmdEndRenderPass(aCmdBuffB);

		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuffB); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
	}

	void submit_commands(lut::VulkanWindow const& aWindow, VkCommandBuffer aCmdBuff, VkCommandBuffer aCmdBuffB, VkFence aFence, VkFence aFenceB, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore, VkSemaphore aSignalSemaphoreB)
	{
		vkResetFences(aWindow.device, 1, &aFence);
		vkResetFences(aWindow.device, 1, &aFenceB);
		//throw lut::Error( "Not yet implemented" ); //TODO: (Section 1/Exercise 3) implement me!
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

		if (auto const res = vkQueueSubmit(aWindow.graphicsQueue, 1, &submitInfo, aFence); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to submit command buffer to queue\n"
				"vkQueueSubmit() returned %s", lut::to_string(res).c_str());
		}
		VkPipelineStageFlags waitStagesB[] = { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT };
		VkSubmitInfo submitInfoB = {};
		submitInfoB.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfoB.commandBufferCount = 1;
		submitInfoB.pCommandBuffers = &aCmdBuffB;
		submitInfoB.waitSemaphoreCount = 1;
		submitInfoB.pWaitSemaphores = &aSignalSemaphore; // Wait for A to signal
		submitInfoB.pWaitDstStageMask = waitStagesB;
		submitInfoB.signalSemaphoreCount = 1;
		submitInfoB.pSignalSemaphores = &aSignalSemaphoreB;
		if (vkQueueSubmit(aWindow.graphicsQueue, 1, &submitInfoB, aFenceB) != VK_SUCCESS) {
			throw std::runtime_error("Failed to submit Command Buffer postprocessing.");
		}
	}

	void present_results(VkQueue aPresentQueue, VkSwapchainKHR aSwapchain, std::uint32_t aImageIndex, VkSemaphore aRenderFinished, bool& aNeedToRecreateSwapchain)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: (Section 1/Exercise 3) implement me!
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &aRenderFinished;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &aSwapchain;
		presentInfo.pImageIndices = &aImageIndex;
		presentInfo.pResults = nullptr;
		const auto presentRes = vkQueuePresentKHR(aPresentQueue, &presentInfo);
		if (VK_SUBOPTIMAL_KHR == presentRes || VK_ERROR_OUT_OF_DATE_KHR == presentRes)
		{
			aNeedToRecreateSwapchain = true;
		}
		else if (VK_SUCCESS != presentRes)
		{
			throw lut::Error("Unable present swapchain image %u\n"
				"vkQueuePresentKHR() returned %s", aImageIndex, lut::to_string(presentRes).c_str());
		}
	}

	//void begin_renderapss(VkCommandBuffer cmdBuffer, VkRenderPass renderPass, VkFramebuffer framebuffer, VkExtent2D extent) {
	//	VkClearValue clearValues[6]{};
	//	for (auto& c : clearValues) {
	//		c.color.float32[0] = 0.1f; // Clear to a dark gray background.
	//		c.color.float32[1] = 0.1f; // If we were debugging, this would potentially
	//		c.color.float32[2] = 0.1f; // help us see whether the render pass took
	//		c.color.float32[3] = 1.f; // place, even if nothing else was drawn.
	//	}
	//	clearValues[5].depthStencil.depth = 1.0f; 
	//	clearValues[5].depthStencil.stencil = 0; 
	//	VkRenderPassBeginInfo passInfo{};
	//	passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	//	passInfo.renderPass = renderPass;
	//	passInfo.framebuffer = framebuffer;
	//	passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
	//	passInfo.renderArea.extent = VkExtent2D{ extent.width, extent.height };
	//	passInfo.clearValueCount = 6;
	//	passInfo.pClearValues = clearValues;
	//	vkCmdBeginRenderPass(cmdBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
	//}
	//void draw_graphics_pipeline(VkCommandBuffer cmdBuffer, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSet sceneSet, std::vector<TMesh> const& meshes, std::vector<VkDescriptorSet> const& descriptors) {
	//	// Begin drawing with our graphics pipeline
	//	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	//	for (size_t i = 0; i < meshes.size(); i++)
	//	{
	//		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &descriptors[i], 0, nullptr);
	//		VkBuffer buffers[3] = { meshes[i].positions.buffer, meshes[i].texcoords.buffer, meshes[i].normals.buffer };
	//		VkDeviceSize offsets[4]{};
	//		vkCmdBindVertexBuffers(cmdBuffer, 0, 3, buffers, offsets);
	//		vkCmdBindIndexBuffer(cmdBuffer, meshes[i].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	//		vkCmdDrawIndexed(cmdBuffer, meshes[i].indexCount, 1, 0, 0, 0);
	//	}
	//}
}

// depth test
namespace
{
	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 6) implement me!
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = cfg::kDepthFormat;
		imageInfo.extent.width = aWindow.swapchainExtent.width;
		imageInfo.extent.height = aWindow.swapchainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 3;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to allocate depth buffer image.\n"
				"vmaCreateImage() returned %s", lut::to_string(res).c_str());
		}
		lut::Image depthImage(aAllocator.allocator, image, allocation);
		VkImageViewCreateInfo viewInfo{};
		viewInfo.subresourceRange.levelCount = 3;
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = depthImage.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = cfg::kDepthFormat;
		viewInfo.components = VkComponentMapping{};
		viewInfo.subresourceRange = VkImageSubresourceRange{
			VK_IMAGE_ASPECT_DEPTH_BIT,
			0,1,
			0,1
		};
		VkImageView view = VK_NULL_HANDLE;
		if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &view); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create image view\n"
				"vkCreateImageView() returned %s", lut::to_string(res).c_str());
		}
		return { std::move(depthImage),lut::ImageView(aWindow.device,view) };
	}

	std::tuple<lut::Image, lut::ImageView> create_gbuffer(lut::VulkanWindow const& aWindow, lut::Allocator const& aAllocator, VkFormat format, VkImageUsageFlags usage) {
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent.width = aWindow.swapchainExtent.width;
		imageInfo.extent.height = aWindow.swapchainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		if (auto const res = vmaCreateImage(aAllocator.allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to allocate depth buffer image.\n"
				"vmaCreateImage() returned %s", lut::to_string(res).c_str());
		}
		lut::Image tImage(aAllocator.allocator, image, allocation);
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = tImage.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = VkImageSubresourceRange{
			VK_IMAGE_ASPECT_COLOR_BIT,
			0,1,
			0,1
		};

		VkImageView view = VK_NULL_HANDLE;
		if (auto const res = vkCreateImageView(aWindow.device, &viewInfo, nullptr, &view); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create image view\n"
				"vkCreateImageView() returned %s", lut::to_string(res).c_str());
		}
		return { std::move(tImage), lut::ImageView(aWindow.device,view)};
	}
}

// update_scene_uniforms
namespace
{
	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, UserState const& aState)
	{
		//TODO- (Section 3) initialize SceneUniform members
		float const aspect = (float)aFramebufferWidth / (float)aFramebufferHeight;
		//The RH indicates a right handed clip space, and the ZO indicates
		//that the clip space extends from zero to one along the Z - axis.
		aSceneUniforms.projection = glm::perspectiveRH_ZO(
			lut::Radians(cfg::kCameraFov).value(),
			aspect,
			cfg::kCameraNear,
			cfg::kCameraFar
		);
		aSceneUniforms.projection[1][1] *= -1.f;
		aSceneUniforms.camera = glm::inverse(aState.camera2world);
		aSceneUniforms.projCam = aSceneUniforms.projection * aSceneUniforms.camera;
		aSceneUniforms.lightPos = aState.lightPos;
		aSceneUniforms.lightCol = aState.lightCol;
		aSceneUniforms.cameraPos = glm::vec3(aState.camera2world[3]);
	}
}


//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
