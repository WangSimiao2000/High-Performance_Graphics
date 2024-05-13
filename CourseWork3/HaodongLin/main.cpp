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
		// Compiled shader code for the graphics pipeline(s)
		// See sources in cw1/shaders/*. 
#		define SHADERDIR_ "assets/cw3/shaders/"
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
		constexpr char const* kFragAlphaShaderPath = SHADERDIR_ "alpha.frag.spv";
		constexpr char const* kVertFullScreenShaderPath = SHADERDIR_ "fullscreen.vert.spv";
		constexpr char const* kFragPostProcessingShaderPath = SHADERDIR_ "postprocessing.frag.spv";
		constexpr char const* kWhiteTexture = "assets-src/cw3/suntemple/white.jpg";
#		undef SHADERDIR_

		// General rule: with a standard 24 bit or 32 bit float depth buffer,
		// you can support a 1:1000 ratio between the near and far plane with
		// minimal depth fighting. Larger ratios will introduce more depth
		// fighting problems; smaller ratios will increase the depth buffer's
		// resolution but will also limit the view distance.
		constexpr float kCameraNear = 0.1f;
		constexpr float kCameraFar = 100.f;

		constexpr auto kCameraFov = 60.0_degf;

		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
		constexpr float kCameraBaseSpeed = 1.7f;
		constexpr float kCameraFastMult = 5.f;
		constexpr float kCameraSlowMult = 0.05f;
		constexpr float kCameraMouseSensitivity = 0.01f;
	}

	using Clock_ = std::chrono::steady_clock;
	using Secondsf_ = std::chrono::duration<float, std::ratio<1>>;
	void glfw_callback_button(GLFWwindow*, int, int, int);
	void glfw_callback_motion(GLFWwindow*, double, double);
	// GLFW callbacks
	void glfw_callback_key_press(GLFWwindow*, int, int, int, int);

	namespace glsl
	{
		struct SceneUniform {
			// Note: need to be careful about the packing/alignment here!
			glm::mat4 camera;
			glm::mat4 projection;
			glm::mat4 projCam;
			glm::vec3 cameraPos;
			float _pad0;
			glm::vec3 lightPos;
			float _pad1;
			glm::vec3 lightCol;
			float _pad2;
		};
		// We want to use vkCmdUpdateBuffer() to update the contents of our uniform buffers. vkCmdUpdateBuffer()
		// has a number of requirements, including the two below.
		static_assert(sizeof(SceneUniform) <= 65536, "SceneUniform must be less than 65536 bytes for vkCmdUpdateBuffer");
		static_assert(sizeof(SceneUniform) % 4 == 0, "SceneUniform size must be a multiple of 4 bytes");
	}

	// Local types/structures:
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
		glm::mat4 camera2world = glm::identity<glm::mat4>();
		glm::vec3 lightPos = glm::vec3(-0.2972, 7.3100, -11.9532);
		glm::vec3 lightCol = glm::vec3(1.0, 1.0, 1.0);
	};

	// Local functions:
	void update_user_state(UserState&, float aElapsedTime);

	lut::RenderPass create_render_pass_a(lut::VulkanWindow const&);
	lut::RenderPass create_render_pass_b(lut::VulkanWindow const&);

	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const&);
	lut::DescriptorSetLayout create_object_descriptor_layout(lut::VulkanWindow const&);
	lut::DescriptorSetLayout create_postprocessing_descriptor_layout(lut::VulkanWindow const& aWindow);
	//lut::DescriptorSetLayout create_alphaObject_descriptor_layout(lut::VulkanWindow const&);

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const&
		, VkDescriptorSetLayout aSceneDescriptorLayout
		, VkDescriptorSetLayout aObjectDescriptorLayout
	);

	lut::PipelineLayout create_post_pipeline_layout(lut::VulkanContext const&
		, VkDescriptorSetLayout aObjectDescriptorLayout
	);

	lut::Pipeline create_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	lut::Pipeline create_alpha_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	lut::Pipeline create_fullscreen_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);


	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const&, lut::Allocator const&);

	void create_swapchain_framebuffers(
		lut::VulkanWindow const&,
		VkRenderPass,
		std::vector<lut::Framebuffer>&,
		VkImageView aDepthView
	);

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
		VkDescriptorSet aPostDescriptors,
		std::vector<BakedMesh> const& aModleMesh,
		std::vector<VkDescriptorSet> const& aDescriptorA,
		std::vector<VkDescriptorSet> const& aDescriptorB,
		std::vector<VkDescriptorSet> const& aDescriptorC,
		VkImage
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

}

int main() try
{
	// Create Vulkan Window
	auto window = lut::make_vulkan_window();
	UserState state{};
	glfwSetWindowUserPointer(window.window, &state);
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);
	glfwSetMouseButtonCallback(window.window, &glfw_callback_button);
	glfwSetCursorPosCallback(window.window, &glfw_callback_motion);
	// Configure the GLFW window
	//TODO- (Section 4) set up user state and connect user input callbacks
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);

	// Create VMA allocator
	lut::Allocator allocator = lut::create_allocator(window);

	// Intialize resources
	lut::RenderPass renderPassA = create_render_pass_a(window);
	lut::RenderPass renderPassB = create_render_pass_b(window);

	lut::DescriptorSetLayout sceneLayout = create_scene_descriptor_layout(window);
	lut::DescriptorSetLayout objectLayout = create_object_descriptor_layout(window);
	lut::DescriptorSetLayout postLayout = create_postprocessing_descriptor_layout(window);

	lut::PipelineLayout pipeLayout = create_pipeline_layout(window, sceneLayout.handle, objectLayout.handle);
	lut::PipelineLayout postPipeLayout = create_post_pipeline_layout(window, postLayout.handle);

	lut::Pipeline pipe = create_pipeline(window, renderPassA.handle, pipeLayout.handle);
	lut::Pipeline pipeB = create_fullscreen_pipeline(window, renderPassB.handle, postPipeLayout.handle);
	lut::Pipeline alphaPipe = create_alpha_pipeline(window, renderPassA.handle, pipeLayout.handle);

	//TODO- (Section 2) create depth buffer
	auto [depthBuffer, depthBufferView] = create_depth_buffer(window, allocator);
	std::vector<lut::Framebuffer> framebuffers;
	create_swapchain_framebuffers(window, renderPassB.handle, framebuffers, depthBufferView.handle);

	lut::CommandPool cpool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> cbuffers;
	std::vector<VkCommandBuffer> cbuffersB;
	std::vector<lut::Fence> cbfences;
	std::vector<lut::Fence> cbfencesB;

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

	// Load data
	BakedModel bakedObject = load_baked_model("assets/cw3/suntemple.comp5822mesh");

	BakedMesh bakedMesh;

	std::vector<BakedMesh> modelMesh;

	for (auto const& mesh : bakedObject.meshes)
	{
		std::vector<glm::vec3> positions = mesh.positions;
		std::vector<glm::vec2> texcoords = mesh.texcoords;
		std::vector<glm::vec3> normals = mesh.normals;
		std::vector<glm::vec4> tangents = mesh.tangents;

		std::vector<std::uint32_t> indices = mesh.indices;
		std::uint32_t index = static_cast<std::uint32_t>(indices.size());
		//std::cout << "tex size " << texcoords.size() << " Normal size " << normals.size() << " tangent size " << tangents.size() << std::endl;
		bakedMesh = create_model_mesh(window, allocator, positions, texcoords, normals, tangents, indices);

		bakedMesh.index = index;
		modelMesh.push_back(std::move(bakedMesh));
	}


	lut::Buffer sceneUBO = lut::create_buffer(
		allocator,
		sizeof(glsl::SceneUniform),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);

	lut::DescriptorPool dpool = lut::create_descriptor_pool(window);
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

	lut::Image intermediateImage = lut::create_image_texture2d(allocator, window.swapchainExtent.width, window.swapchainExtent.height, VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	lut::ImageView intermediateImageView = lut::create_image_view_texture2d(window, intermediateImage.image, VK_FORMAT_R8G8B8A8_UNORM);
	VkImageView attachments[2] = { intermediateImageView.handle, depthBufferView.handle };
	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPassA.handle;  // 使用适当的渲染通道
	framebufferInfo.attachmentCount = 2;
	framebufferInfo.pAttachments = attachments;  // 中间图像视图
	framebufferInfo.width = window.swapchainExtent.width;
	framebufferInfo.height = window.swapchainExtent.height;
	framebufferInfo.layers = 1;
	lut::Framebuffer intermediateFramBuffer;
	vkCreateFramebuffer(window.device, &framebufferInfo, nullptr, &intermediateFramBuffer.handle);
	lut::Sampler intermediateSampler = lut::create_default_sampler(window);
	lut::CommandPool intermediateCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	VkDescriptorSet intermediateDescriptors = lut::alloc_desc_set(window, dpool.handle, postLayout.handle);
	{
		VkWriteDescriptorSet desc[1]{};
		VkDescriptorImageInfo intermediateInfo{};
		intermediateInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		intermediateInfo.imageView = intermediateImageView.handle;
		intermediateInfo.sampler = intermediateSampler.handle;
		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = intermediateDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc[0].descriptorCount = 1;
		desc[0].pImageInfo = &intermediateInfo;
		/* write in descriptor set */
		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}

	std::vector<VkDescriptorSet> descriptors;
	std::vector<lut::ImageView> texViews;
	lut::Sampler texSamples = lut::create_default_sampler(window);
	std::vector<lut::Image> texImages;
	lut::CommandPool loadCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// descriptor of material color
	std::vector<VkDescriptorSet> mtlDes;
	std::vector<lut::ImageView> mtlViews;
	lut::Sampler mtlSamples = lut::create_default_sampler(window);
	std::vector<lut::Image> mtlImages;
	lut::CommandPool loadCmdPool_mtl = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// descriptor of roughtness color
	std::vector<VkDescriptorSet> rogDes;
	std::vector<lut::ImageView> rogViews;
	lut::Sampler rogSamples = lut::create_default_sampler(window);
	std::vector<lut::Image> rogImages;
	lut::CommandPool loadCmdPool_rog = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	// descriptor of normalmap
	std::vector<VkDescriptorSet> norDes;
	std::vector<lut::ImageView> norViews;
	lut::Sampler norSamples = lut::create_default_sampler(window);
	std::vector<lut::Image> norImages;
	lut::CommandPool loadCmdPool_nor = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	std::vector<VkDescriptorSet> alpDes;
	std::vector<lut::ImageView> alpViews;
	lut::Sampler alpSamples = lut::create_default_sampler(window);
	std::vector<lut::Image> alpImages;
	lut::CommandPool loadCmdPool_alp = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

	{
		for (std::size_t i = 0; i < bakedObject.meshes.size(); i++)
		{
			int materialId = bakedObject.meshes[i].materialId;

			const auto& texInfo = bakedObject.textures[bakedObject.materials[materialId].baseColorTextureId];
			VkDescriptorSet ds = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			std::string texturePath = texInfo.path;

			texImages.push_back(lut::load_image_texture2d(texturePath.c_str(), window, loadCmdPool.handle, allocator));
			texViews.push_back(lut::create_image_view_texture2d(window, texImages[i].image, VK_FORMAT_R8G8B8A8_SRGB));

			VkWriteDescriptorSet desc[5]{};
			VkDescriptorImageInfo textureInfo{};
			textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			textureInfo.imageView = texViews[i].handle;
			textureInfo.sampler = texSamples.handle;
			desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[0].dstSet = ds;
			desc[0].dstBinding = 0;
			desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[0].descriptorCount = 1;
			desc[0].pImageInfo = &textureInfo;

			const auto& mtlInfo = bakedObject.textures[bakedObject.materials[materialId].metalnessTextureId];
			//VkDescriptorSet mtlDs = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			std::string mtlTexturePath = mtlInfo.path;

			mtlImages.push_back(lut::load_image_texture2d(mtlTexturePath.c_str(), window, loadCmdPool_mtl.handle, allocator));
			mtlViews.push_back(lut::create_image_view_texture2d(window, mtlImages[i].image, VK_FORMAT_R8G8B8A8_UNORM));

			//VkWriteDescriptorSet mtlDesc[1]{};
			VkDescriptorImageInfo mtlTexInfo{};
			mtlTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			mtlTexInfo.imageView = mtlViews[i].handle;
			mtlTexInfo.sampler = mtlSamples.handle;
			desc[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[1].dstSet = ds;
			desc[1].dstBinding = 1;
			desc[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[1].descriptorCount = 1;
			desc[1].pImageInfo = &mtlTexInfo;

			const auto& rogInfo = bakedObject.textures[bakedObject.materials[materialId].roughnessTextureId];
			//VkDescriptorSet rogDs = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			std::string rogTexturePath = rogInfo.path;

			rogImages.push_back(lut::load_image_texture2d(rogTexturePath.c_str(), window, loadCmdPool_rog.handle, allocator));
			rogViews.push_back(lut::create_image_view_texture2d(window, rogImages[i].image, VK_FORMAT_R8G8B8A8_UNORM));

			//VkWriteDescriptorSet rogDesc[1]{};
			VkDescriptorImageInfo rogTexInfo{};
			rogTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rogTexInfo.imageView = rogViews[i].handle;
			rogTexInfo.sampler = rogSamples.handle;
			desc[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[2].dstSet = ds;
			desc[2].dstBinding = 2;
			desc[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[2].descriptorCount = 1;
			desc[2].pImageInfo = &rogTexInfo;

			//std::cout << bakedObject.materials[materialId].normalMapTextureId << std::endl;
			//VkDescriptorSet norDs = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			if (bakedObject.materials[materialId].normalMapTextureId == 0xffffffff)
			{
				norImages.push_back(lut::load_image_texture2d("assets-src/cw2/rgba1111.png", window, loadCmdPool_nor.handle, allocator, VK_FORMAT_R8G8B8A8_UNORM));
				norViews.push_back(lut::create_image_view_texture2d(window, norImages[i].image, VK_FORMAT_R8G8B8A8_UNORM));
			}
			else
			{
				const auto& norInfo = bakedObject.textures[bakedObject.materials[materialId].normalMapTextureId];
				std::string norTexturePath = norInfo.path;
				norImages.push_back(lut::load_image_texture2d(norTexturePath.c_str(), window, loadCmdPool_nor.handle, allocator, VK_FORMAT_R8G8B8A8_UNORM));
				norViews.push_back(lut::create_image_view_texture2d(window, norImages[i].image, VK_FORMAT_R8G8B8A8_UNORM));
			}

			///VkWriteDescriptorSet norDesc[1]{};
			VkDescriptorImageInfo norTexInfo{};
			norTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			norTexInfo.imageView = norViews[i].handle;
			norTexInfo.sampler = norSamples.handle;
			desc[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[3].dstSet = ds;
			desc[3].dstBinding = 3;
			desc[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[3].descriptorCount = 1;
			desc[3].pImageInfo = &norTexInfo;

			//VkDescriptorSet alpDs = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			if (bakedObject.materials[materialId].alphaMaskTextureId == 0xffffffff)
			{
				modelMesh[i].useAlphaMask = false;
				alpImages.push_back(lut::load_image_texture2d("assets-src/cw3/rgba1111.png", window, loadCmdPool_nor.handle, allocator));
				alpViews.push_back(lut::create_image_view_texture2d(window, alpImages[i].image, VK_FORMAT_R8G8B8A8_SRGB));
			}
			else
			{
				modelMesh[i].useAlphaMask = true;
				const auto& alpInfo = bakedObject.textures[bakedObject.materials[materialId].alphaMaskTextureId];
				std::string alpTexturePath = alpInfo.path;
				alpImages.push_back(lut::load_image_texture2d(alpTexturePath.c_str(), window, loadCmdPool_alp.handle, allocator));
				alpViews.push_back(lut::create_image_view_texture2d(window, alpImages[i].image, VK_FORMAT_R8G8B8A8_SRGB));
			}

			//VkWriteDescriptorSet alpDesc[1]{};
			VkDescriptorImageInfo alpTexInfo{};
			alpTexInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			alpTexInfo.imageView = alpViews[i].handle;
			alpTexInfo.sampler = alpSamples.handle;
			desc[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc[4].dstSet = ds;
			desc[4].dstBinding = 4;
			desc[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			desc[4].descriptorCount = 1;
			desc[4].pImageInfo = &alpTexInfo;

			vkUpdateDescriptorSets(window.device, 5, desc, 0, nullptr);
			descriptors.push_back(std::move(ds));
		}
	}

	// Application main loop
	bool recreateSwapchain = false;

	auto previousClock = Clock_::now();
	while (!glfwWindowShouldClose(window.window))
	{
		glfwPollEvents(); // or: glfwWaitEvents()

		// Recreate swap chain?
		if (recreateSwapchain)
		{
			vkDeviceWaitIdle(window.device);
			auto const changes = recreate_swapchain(window);
			if (changes.changedFormat)
			{
				renderPassA = create_render_pass_a(window);
				renderPassB = create_render_pass_b(window);
			}
			if (changes.changedSize)
				std::tie(depthBuffer, depthBufferView) = create_depth_buffer(window, allocator);
			framebuffers.clear();
			create_swapchain_framebuffers(window, renderPassB.handle, framebuffers, depthBufferView.handle);
			if (changes.changedSize)
			{
				pipe = create_pipeline(window, renderPassA.handle, pipeLayout.handle);
				pipeB = create_fullscreen_pipeline(window, renderPassB.handle, pipeLayout.handle);
				//unTexPipe = create_unTex_pipeline(window, renderPass.handle, pipeLayout.handle);
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

		assert(std::size_t(imageIndex) < cbuffers.size());
		assert(std::size_t(imageIndex) < framebuffers.size());

		glsl::SceneUniform sceneUniforms{};
		update_scene_uniforms(sceneUniforms, window.swapchainExtent.width, window.swapchainExtent.height, state);

		record_commands(
			cbuffers[imageIndex],
			cbuffersB[imageIndex],
			renderPassA.handle,
			renderPassB.handle,
			framebuffers[imageIndex].handle,
			intermediateFramBuffer.handle,
			pipe.handle,
			pipeB.handle,
			alphaPipe.handle,
			window.swapchainExtent,
			bakedMesh.positions.buffer,
			bakedMesh.texcoords.buffer,
			bakedMesh.normals.buffer,
			bakedMesh.index,
			sceneUBO.buffer,
			sceneUniforms,
			pipeLayout.handle,
			postPipeLayout.handle,
			sceneDescriptors,
			intermediateDescriptors,
			modelMesh,
			descriptors,
			mtlDes,
			rogDes,
			intermediateImage.image
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

	// Cleanup takes place automatically in the destructors, but we sill need
	// to ensure that all Vulkan commands have finished before that.
	vkDeviceWaitIdle(window.device);

	return 0;
}
catch (std::exception const& eErr)
{
	std::fprintf(stderr, "\n");
	std::fprintf(stderr, "Error: %s\n", eErr.what());
	return 1;
}

namespace
{
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

	lut::RenderPass create_render_pass_a(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
		VkAttachmentDescription attachments[2]{};
		attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

		VkSubpassDependency deps[3]{};
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

	lut::RenderPass create_render_pass_b(lut::VulkanWindow const& aWindow)
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

namespace
{
	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, UserState const& aState)
	{
		//TODO- (Section 3) initialize SceneUniform members
		float const aspect = (float)aFramebufferWidth / aFramebufferHeight;
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

	lut::PipelineLayout create_post_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aObjectLayout
	)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
		VkDescriptorSetLayout layouts[] = {
			aObjectLayout
		};
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
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

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout
	)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1 / Exercise 3) implement me!
		VkDescriptorSetLayout layouts[] = {
			aSceneLayout,
			aObjectLayout
		};
		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 2;
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

	lut::Pipeline create_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
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
		VkVertexInputBindingDescription vertexInputs[4]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 2;
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[2].binding = 2;
		vertexInputs[2].stride = sizeof(float) * 3;
		vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[3].binding = 3;
		vertexInputs[3].stride = sizeof(float) * 4;
		vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[4]{};
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

		vertexAttributes[3].binding = 3;
		vertexAttributes[3].location = 3;
		vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttributes[3].offset = 0;

		inputInfo.vertexBindingDescriptionCount = 4;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 4;
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

	lut::Pipeline create_fullscreen_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertFullScreenShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragPostProcessingShaderPath);
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
		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthInfo.depthTestEnable = VK_FALSE;
		depthInfo.depthWriteEnable = VK_FALSE;
		depthInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;
		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		inputInfo.vertexBindingDescriptionCount = 0;
		inputInfo.pVertexBindingDescriptions = nullptr;
		inputInfo.vertexAttributeDescriptionCount = 0;
		inputInfo.pVertexAttributeDescriptions = nullptr;
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

	lut::Pipeline create_alpha_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragAlphaShaderPath);
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
		VkVertexInputBindingDescription vertexInputs[4]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 2;
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[2].binding = 2;
		vertexInputs[2].stride = sizeof(float) * 3;
		vertexInputs[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[3].binding = 3;
		vertexInputs[3].stride = sizeof(float) * 4;
		vertexInputs[3].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[4]{};
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

		vertexAttributes[3].binding = 3;
		vertexAttributes[3].location = 3;
		vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttributes[3].offset = 0;

		inputInfo.vertexBindingDescriptionCount = 4;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 4;
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

	void create_swapchain_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers, VkImageView aDepthView)
	{
		assert(aFramebuffers.empty());
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		for (std::size_t i = 0; i < aWindow.swapViews.size(); ++i)
		{
			VkImageView attachments[2] = {
				aWindow.swapViews[i],
				aDepthView
			};
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0;
			fbInfo.renderPass = aRenderPass;
			fbInfo.attachmentCount = 2;
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

	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 3) implement me!
		VkDescriptorSetLayoutBinding bindings[1]{};
		// Input vertex buffer
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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

	lut::DescriptorSetLayout create_postprocessing_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO: (Section 4) implement me!
		VkDescriptorSetLayoutBinding bindings[1]{};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;// sizeof(bindings) / sizeof(bindings[0]);
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
		VkDescriptorSetLayoutBinding bindings[5]{};
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

		bindings[3].binding = 3;
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[4].binding = 4;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[4].descriptorCount = 1;
		bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 5;// sizeof(bindings) / sizeof(bindings[0]);
		layoutInfo.pBindings = bindings;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;

		if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create descriptor set layout\n"
				"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
		}
		return lut::DescriptorSetLayout(aWindow.device, layout);
	}

	// lut::DescriptorSetLayout create_alphaObject_descriptor_layout(lut::VulkanWindow const& aWindow)
	// {
	// 	//throw lut::Error( "Not yet implemented" ); //TODO: (Section 4) implement me!
	// 	VkDescriptorSetLayoutBinding bindings[4]{};
	// 	bindings[0].binding = 0;
	// 	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	// 	bindings[0].descriptorCount = 1;
	// 	bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// 	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	// 	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	// 	layoutInfo.bindingCount = 1;// sizeof(bindings) / sizeof(bindings[0]);
	// 	layoutInfo.pBindings = bindings;
	// 	VkDescriptorSetLayout layout = VK_NULL_HANDLE;

	// 	if (auto const res = vkCreateDescriptorSetLayout(aWindow.device, &layoutInfo, nullptr, &layout); VK_SUCCESS != res)
	// 	{
	// 		throw lut::Error("Unable to create descriptor set layout\n"
	// 			"vkCreateDescriptorSetLayout() returned %s", lut::to_string(res).c_str());
	// 	}
	// 	return lut::DescriptorSetLayout(aWindow.device, layout);
	// }

	void record_commands(
		VkCommandBuffer aCmdBuff,
		VkCommandBuffer aCmdBuffB,
		VkRenderPass aRenderPassA,
		VkRenderPass aRenderPassB,
		VkFramebuffer aFramebuffer,
		VkFramebuffer aIntermediateFramebuffer,
		VkPipeline aGraphicsPipe,
		VkPipeline aGraphicsPipeB,
		VkPipeline aAlphaPipe,
		VkExtent2D const& aImageExtent,
		VkBuffer aPositionBuffer,
		VkBuffer TexCoordBuffer,
		VkBuffer normalBuffer,
		std::uint32_t aVertexCount,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const& aSceneUniform,
		VkPipelineLayout aGraphicsLayout,
		VkPipelineLayout aGraphicsLayoutB,
		VkDescriptorSet aSceneDescriptors,
		VkDescriptorSet aIntermediateDescriptors,
		//VkDescriptorSet aObjectDecriptors,
		std::vector<BakedMesh> const& aModelMesh,
		std::vector<VkDescriptorSet> const& aDescriptorA,
		std::vector<VkDescriptorSet> const& aDescriptorB,
		std::vector<VkDescriptorSet> const& aDescriptorC,
		VkImage intermediateImage
	)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 1/Exercise 3) implement me!
		// Begin recording commands
		VkCommandBufferBeginInfo begInfo{};
		begInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
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
		//std::cout << aSceneUniform.lightPos.x << aSceneUniform.lightPos.y << aSceneUniform.lightPos.z << "                       c" << std::endl;
		// Begin render pass
		VkClearValue clearValues[2]{};
		clearValues[0].color.float32[0] = 0.1f; // Clear to a dark gray background.
		clearValues[0].color.float32[1] = 0.1f; // If we were debugging, this would potentially
		clearValues[0].color.float32[2] = 0.1f; // help us see whether the render pass took
		clearValues[0].color.float32[3] = 1.f; // place, even if nothing else was drawn.
		clearValues[1].depthStencil.depth = 1.f;
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPassA;
		passInfo.framebuffer = aIntermediateFramebuffer;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 2;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		// Begin drawing with our graphics pipeline

		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		//vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

		for (std::size_t i = 0; i < aModelMesh.size(); i++)
		{
			if (!aModelMesh[i].useAlphaMask)
			{
				vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aDescriptorA[i], 0, nullptr);
			}
			else
			{
				vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aAlphaPipe);
				vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aDescriptorA[i], 0, nullptr);
			}

			VkBuffer buffers[4] = { aModelMesh[i].positions.buffer, aModelMesh[i].texcoords.buffer, aModelMesh[i].normals.buffer, aModelMesh[i].tangents.buffer };
			VkDeviceSize offsets[4]{};
			vkCmdBindVertexBuffers(aCmdBuff, 0, 4, buffers, offsets);
			vkCmdBindIndexBuffer(aCmdBuff, aModelMesh[i].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(aCmdBuff, aModelMesh[i].index, 1, 0, 0, 0);
		}

		// End the render pass
		vkCmdEndRenderPass(aCmdBuff);

		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}

		VkCommandBufferBeginInfo begInfoB = {};
		begInfoB.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begInfoB.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

		if (vkBeginCommandBuffer(aCmdBuffB, &begInfoB) != VK_SUCCESS) {
			throw std::runtime_error("Failed to begin recording command buffer for Render Pass B.");
		}

		VkRenderPassBeginInfo passInfoB = {};
		passInfoB.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfoB.renderPass = aRenderPassB;
		passInfoB.framebuffer = aFramebuffer;
		passInfoB.renderArea.offset = { 0, 0 };
		passInfoB.renderArea.extent = aImageExtent;
		passInfoB.clearValueCount = 2;
		passInfoB.pClearValues = clearValues;

		vkCmdBeginRenderPass(aCmdBuffB, &passInfoB, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(aCmdBuffB, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipeB);
		vkCmdBindDescriptorSets(aCmdBuffB, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayoutB, 0, 1, &aIntermediateDescriptors, 0, nullptr);

		// Drawing full screen quad (assuming you have a vertex shader that outputs a full screen quad)
		vkCmdDraw(aCmdBuffB, 3, 1, 0, 0);  // Just 3 vertices, drawing a full screen triangle
		vkCmdEndRenderPass(aCmdBuffB);

		if (vkEndCommandBuffer(aCmdBuffB) != VK_SUCCESS) {
			throw std::runtime_error("Failed to record command buffer.");
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
			throw std::runtime_error("Failed to submit Command Buffer B.");
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
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 


//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
