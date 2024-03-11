#include <volk/volk.h>
#include<iostream>
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

#include "simple_model.hpp"
#include "load_model_obj.hpp"
#include "vertex_data.hpp"
#include "current_Version.hpp"

namespace
{
	namespace cfg
	{
		// Compiled shader code for the graphics pipeline(s)
		// See sources in cw1/shaders/*. 
#		define SHADERDIR_ "assets/cw1/shaders/"
#if CURRENT_VERSION == DEFAULT
		constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "colorized.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == MIPMAP_VISUALIZE_LINEAR || CURRENT_VERSION == MIPMAP_VISUALIZE_NEAREST
		constexpr char const* kFragShaderPath = SHADERDIR_ "mipmapVisualize.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "colorized.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == FRAGMENT_DEPTH
		constexpr char const* kFragShaderPath = SHADERDIR_ "fragmentDepth.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "fragmentDepth.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == PARTIAL_DERIVATIVES_FRAGMENT_DEPTH
		constexpr char const* kFragShaderPath = SHADERDIR_ "partialDerivativesFragmentDepth.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "partialDerivativesFragmentDepth.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == OVERDRAW_VISUALIZE
		constexpr char const* kFragShaderPath = SHADERDIR_ "overDraw_overShading.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "overDraw_overShading.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == OVERSHADING_VISUALIZE
		constexpr char const* kFragShaderPath = SHADERDIR_ "overDraw_overShading.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "overDraw_overShading.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#elif CURRENT_VERSION == MESHDENSITY_VISUALIZE
		constexpr char const* kFragShaderPath = SHADERDIR_ "meshDensity.frag.spv";
		constexpr char const* kMeshDensityShaderPath = SHADERDIR_ "meshDensity.geom.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "meshDensity.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "colorized.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#else
		constexpr char const* kFragShaderPath = SHADERDIR_ "default.frag.spv";
		constexpr char const* kVertShaderPath = SHADERDIR_ "default.vert.spv";
		constexpr char const* kColorizedFragShaderPath = SHADERDIR_ "colorized.frag.spv";
		constexpr char const* kColorizedVertShaderPath = SHADERDIR_ "colorized.vert.spv";
#endif 
#		undef SHADERDIR_

		// Textures
#		define ASSERTDIR_ "assets/cw1/textures/jpeg/"
		constexpr char const* kSpriteTexture = ASSERTDIR_ "white.png";
		//constexpr char const* kSpriteTexture = ASSERTDIR_ "sponza_curtain_blue_diff.jpg";
#		undef ASSERTDIR_

		constexpr char const* kModelPath = "assets/cw1/sponza_with_ship.obj";

		// General rule: with a standard 24 bit or 32 bit float depth buffer,
		// you can support a 1:1000 ratio between the near and far plane with
		// minimal depth fighting. Larger ratios will introduce more depth
		// fighting problems; smaller ratios will increase the depth buffer's
		// resolution but will also limit the view distance.
		constexpr float kCameraNear  = 0.1f;
		constexpr float kCameraFar   = 100.f;
		constexpr auto kCameraFov    = 60.0_degf;
		constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
		constexpr float kCameraBaseSpeed = 2.0f;
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


	// Local types/structures:
	namespace glsl
	{
		struct SceneUniform {
			// Note: need to be careful about the packing/alignment here!
			glm::mat4 camera;
			glm::mat4 projection;
			glm::mat4 projCam;
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
		glm::mat4 camera2world = glm::identity<glm::mat4>();
	};

	void update_user_state(UserState&, float aElapsedTime);

	// Local functions:
	lut::RenderPass create_render_pass(lut::VulkanWindow const&);

	/*lut::PipelineLayout create_triangle_pipeline_layout(lut::VulkanContext const&);
	lut::Pipeline create_triangle_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);*/
	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const&, VkDescriptorSetLayout, VkDescriptorSetLayout);
	lut::Pipeline create_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	lut::Pipeline create_colorized_pipeline(lut::VulkanWindow const&, VkRenderPass, VkPipelineLayout);
	std::tuple<lut::Image, lut::ImageView> create_depth_buffer(lut::VulkanWindow const&, lut::Allocator const&);

	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const&);
	lut::DescriptorSetLayout create_object_descriptor_layout(lut::VulkanWindow const&);

	//creat model mesh
	ColorizedMesh create_color_mesh(labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator, std::vector<float> vertexData, std::vector<glm::vec3> colors);
	TexturedMesh create_model_mesh(labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator, std::vector<float> vertexDate, std::vector<float> texCoords);

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
		UserState const& aState
	);

	void record_commands(
		VkCommandBuffer,
		VkRenderPass,
		VkFramebuffer,
		VkPipeline,
		VkPipeline,
		VkExtent2D const&,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const&,
		VkPipelineLayout,
		VkDescriptorSet aSceneDescriptors,
		VkDescriptorSet aSpriteObjDescriptors,
		std::vector<TexturedMesh> const& aModelmesh,
		std::vector<ColorizedMesh> const& aColorizedmesh,
		std::vector<VkDescriptorSet> aModelDescriptors
	);
	void submit_commands(
		lut::VulkanContext const&,
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
	//TODO-implement me.
	// en: Creat vulkan instance
	// zh: 创建vulkan实例
	
	// en: Create our Vulkan Window
	// zh: 创建Vulkan窗口window
	lut::VulkanWindow window = lut::make_vulkan_window();

	std::uint32_t loaderVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
	if (vkEnumerateInstanceVersion)
	{
		if (auto const res = vkEnumerateInstanceVersion(&loaderVersion); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Warning: vkEnumerateInstanceVersion() returned error %s\n", lut::to_string(res).c_str());
		}
	}
	std::printf("Vulkan loader version: %d.%d.%d (variant %d)\n", VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion), VK_API_VERSION_PATCH(loaderVersion), VK_API_VERSION_VARIANT(loaderVersion));


	// en: Setting window callback functions using the GLFW library.
	// zh: 使用 GLFW 库设置窗口的回调函数
	UserState state{};
	glfwSetWindowUserPointer(window.window, &state); // 这行代码将指向 state 对象的指针与 GLFW 窗口关联起来
	glfwSetKeyCallback(window.window, &glfw_callback_key_press);// 当用户按下或释放键盘上的按键时, 会调用 glfw_callback_key_press 函数
	glfwSetMouseButtonCallback(window.window, &glfw_callback_button); // 当用户按下或释放鼠标上的按钮时,会调用 glfw_callback_button 函数
	glfwSetCursorPosCallback(window.window, &glfw_callback_motion); // 当用户移动鼠标时,会调用 glfw_callback_motion 函数

	// en: Create VMA allocator
	// zh: 创建一个名为 allocator 的 VMA 分配器（Vulkan Memory Allocator）
	lut::Allocator allocator = lut::create_allocator(window); // 窗口作为参数传递给create_allocator函数

	// en: Intialize resources
	// zh: 初始化资源
	// en: render pass
	// zh: 调用 create_render_pass(window) 函数创建一个名为 renderPass 的渲染通道
	lut::RenderPass renderPass = create_render_pass(window); 
	// en: Calling the create_scene_descriptor_layout(window)function creates a descriptor set layout named sceneLayout.
	// zh: 调用 create_scene_descriptor_layout(window)函数创建一个名为 sceneLayout 的描述符集布局
	lut::DescriptorSetLayout sceneLayout = create_scene_descriptor_layout(window);
	// en: Calling the create_object_descriptor_layout(window)function creates a descriptor set layout named objectLayout.
	// zh: 调用 create_object_descriptor_layout(window)函数创建一个名为 objectLayout 的描述符集布局
	lut::DescriptorSetLayout objectLayout = create_object_descriptor_layout(window);

	// pipeline
	lut::PipelineLayout pipeLayout = create_pipeline_layout(window, sceneLayout.handle, objectLayout.handle);
	lut::Pipeline pipe = create_pipeline(window, renderPass.handle, pipeLayout.handle);
	lut::Pipeline aircraftPipe = create_colorized_pipeline(window, renderPass.handle, pipeLayout.handle);
	
	// create depth buffer
	auto [depthBuffer, depthBufferView] = create_depth_buffer(window, allocator);
	
	// en: Creates swapchain framebuffers 
	// zh: 创建交换链帧缓冲
	std::vector<lut::Framebuffer> framebuffers;
	create_swapchain_framebuffers(window, renderPass.handle, framebuffers, depthBufferView.handle);

	// Command
	// VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 短暂的
	// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT: 允许重置命令缓冲
	// Command Pool: 命令缓冲池允许你有效地分配,重用和管理命令缓冲(Command Buffer),以便在图形渲染管线中执行各种渲染和计算操作
	// Command Buffer: 包含了一系列的渲染指令或计算指令，用于描述图形渲染管线或计算管线的操作。这些指令可以包括顶点缓冲的绑定、着色器程序的调用、纹理和缓冲对象的绑定以及其他图形或计算相关的操作
	lut::CommandPool cpool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	std::vector<VkCommandBuffer> cbuffers; // cbuffers 向量将保存一系列的 Vulkan 命令缓冲
	std::vector<lut::Fence> cbfences; // 用来存储自定义的 Fence 对象。用于在图形渲染过程中进行同步，以确保命令缓冲的正确执行状态

	// 过循环遍历 framebuffers 向量的大小，为每个cbuffers和cbfences初始化
	for (std::size_t i = 0; i < framebuffers.size(); ++i) 
	{
		cbuffers.emplace_back(lut::alloc_command_buffer(window, cpool.handle));
		cbfences.emplace_back(lut::create_fence(window, VK_FENCE_CREATE_SIGNALED_BIT));
	}

	//semaphore
	lut::Semaphore imageAvailable = lut::create_semaphore(window);
	lut::Semaphore renderFinished = lut::create_semaphore(window);

	lut::Sampler defaultSampler = lut::create_default_sampler(window);
	

	//Create descriptor pool
	lut::DescriptorPool dpool = lut::create_descriptor_pool(window);

	SimpleModel Model = load_simple_wavefront_obj(cfg::kModelPath);
	//obj creat TexturedMesh
	std::vector<TexturedMesh> modelMesh;
	std::vector<ColorizedMesh> colorizedMesh;
	//texture
	lut::Image spriteTex;
	std::vector<lut::Image> modelTex;

	std::vector<VkDescriptorSet> modelDescriptors;
	lut::ImageView spriteView;
	{
		lut::CommandPool loadCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
		spriteTex = lut::load_image_texture2d(cfg::kSpriteTexture, window, loadCmdPool.handle, allocator);
		spriteView = lut::create_image_view_texture2d(window, spriteTex.image, VK_FORMAT_R8G8B8A8_SRGB);
	}
	std::vector<lut::ImageView> modelView;	

	VkDescriptorSet tempDescriptors = VK_NULL_HANDLE;
	std::cout << "processing model data" << std::endl;
	//process simple model data
	for(auto const& mesh:Model.meshes)
	{

		if(mesh.textured)
		{
			std::vector<float> tempPos;
			std::vector<float> tempTex;
			for (std::size_t i = 0; i < mesh.vertexCount; i++)
			{
				tempPos.push_back(Model.dataTextured.positions[mesh.vertexStartIndex + i].x);
				tempPos.push_back(Model.dataTextured.positions[mesh.vertexStartIndex + i].y);
				tempPos.push_back(Model.dataTextured.positions[mesh.vertexStartIndex + i].z);

				tempTex.push_back(Model.dataTextured.texcoords[mesh.vertexStartIndex + i].x);
				tempTex.push_back(Model.dataTextured.texcoords[mesh.vertexStartIndex + i].y);
			}

			{
				lut::CommandPool loadCmdPool = lut::create_command_pool(window, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
				modelTex.push_back(lut::load_image_texture2d(Model.materials[mesh.materialIndex].diffuseTexturePath.c_str(), window, loadCmdPool.handle, allocator));
				modelView.push_back(lut::create_image_view_texture2d(window, modelTex.back().image, VK_FORMAT_R8G8B8A8_SRGB));
			}			

			tempDescriptors = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
			{
				VkWriteDescriptorSet desc[1]{};
				VkDescriptorImageInfo textureInfo{};
				textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				textureInfo.imageView = modelView.back().handle;
				textureInfo.sampler = defaultSampler.handle;
				desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				desc[0].dstSet = tempDescriptors;
				desc[0].dstBinding = 0;
				desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				desc[0].descriptorCount = 1;
				desc[0].pImageInfo = &textureInfo;
				constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
				vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
			}
			TexturedMesh tempMesh = create_model_mesh(window, allocator, tempPos, tempTex);
			modelMesh.push_back(std::move(tempMesh));
			modelDescriptors.push_back(std::move(tempDescriptors));
		}
		else
		{
			std::vector<float> aircraftTempPos;
			std::vector<glm::vec3> aircraftColor;
			for (std::size_t i = 0; i < mesh.vertexCount; i++)
			{
				aircraftTempPos.push_back(Model.dataUntextured.positions[mesh.vertexStartIndex + i].x);
				aircraftTempPos.push_back(Model.dataUntextured.positions[mesh.vertexStartIndex + i].y);
				aircraftTempPos.push_back(Model.dataUntextured.positions[mesh.vertexStartIndex + i].z);
				aircraftColor.push_back(Model.materials[mesh.materialIndex].diffuseColor);
				aircraftColor.push_back(Model.materials[mesh.materialIndex].diffuseColor);
				aircraftColor.push_back(Model.materials[mesh.materialIndex].diffuseColor);
			}
			ColorizedMesh tempMesh = create_color_mesh(window, allocator, aircraftTempPos, aircraftColor);
			colorizedMesh.push_back(std::move(tempMesh));
		}
	}
	std::cout << "done" << std::endl;

	//create scene uniform buffer with lut::create_buffer()
	lut::Buffer sceneUBO = lut::create_buffer(
		allocator,
		sizeof(glsl::SceneUniform),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
	);
	//allocate descriptor set for uniform buffer

	//initialize descriptor set with vkUpdateDescriptorSets
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
		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}
	
	VkDescriptorSet spriteDescriptors = lut::alloc_desc_set(window, dpool.handle, objectLayout.handle);
	{
		VkWriteDescriptorSet desc[1]{};
		VkDescriptorImageInfo textureInfo{};
		textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		textureInfo.imageView = spriteView.handle;
		textureInfo.sampler = defaultSampler.handle;
		desc[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc[0].dstSet = spriteDescriptors;
		desc[0].dstBinding = 0;
		desc[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc[0].descriptorCount = 1;
		desc[0].pImageInfo = &textureInfo;
		constexpr auto numSets = sizeof(desc) / sizeof(desc[0]);
		vkUpdateDescriptorSets(window.device, numSets, desc, 0, nullptr);
	}

	// Application main loop
	bool recreateSwapchain = false;
	auto previousClock = Clock_::now();
	while (!glfwWindowShouldClose(window.window))
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

		if (recreateSwapchain)
		{
			//Tre-create swapchain and associated resources!
			vkDeviceWaitIdle(window.device);
			auto const changes = recreate_swapchain(window);
			if (changes.changedFormat)
				renderPass = create_render_pass(window);
			if (changes.changedSize)
				std::tie(depthBuffer, depthBufferView) = create_depth_buffer(window, allocator);
			framebuffers.clear();
			create_swapchain_framebuffers(window, renderPass.handle, framebuffers,depthBufferView.handle);
			if (changes.changedSize)
			{
				pipe = create_pipeline(window, renderPass.handle, pipeLayout.handle);
				aircraftPipe = create_colorized_pipeline(window, renderPass.handle, pipeLayout.handle);
			}
			recreateSwapchain = false;
			continue;
		}

		//acquire swapchain image.
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

		//record and submit commands
		assert(std::size_t(imageIndex) < cbuffers.size());
		assert(std::size_t(imageIndex) < framebuffers.size());

		// Prepare data for this frame
		glsl::SceneUniform sceneUniforms{};
		update_scene_uniforms(sceneUniforms, window.swapchainExtent.width, window.swapchainExtent.height,state);
		record_commands(
			cbuffers[imageIndex],
			renderPass.handle,
			framebuffers[imageIndex].handle,
			pipe.handle,
			aircraftPipe.handle,
			window.swapchainExtent,
			sceneUBO.buffer,
			sceneUniforms,
			pipeLayout.handle,
			sceneDescriptors,
			spriteDescriptors,
			modelMesh,
			colorizedMesh,
			modelDescriptors
		);
		submit_commands(
			window,
			cbuffers[imageIndex],
			cbfences[imageIndex].handle,
			imageAvailable.handle,
			renderFinished.handle
		);

		//present rendered images.
		present_results(window.presentQueue, window.swapchain, imageIndex, renderFinished.handle, recreateSwapchain);
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

		// 添加处理数字键按下事件的逻辑
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

	lut::RenderPass create_render_pass(lut::VulkanWindow const& aWindow)
	{
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
		subpassAttachments[0].attachment = 0; // the zero refers to attachments[0] declared earlier.
		subpassAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference depthAttachment{};
		depthAttachment.attachment = 1;
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkSubpassDescription subpasses[1]{};
		subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpasses[0].colorAttachmentCount = 1;
		subpasses[0].pColorAttachments = subpassAttachments;
		subpasses[0].pDepthStencilAttachment = &depthAttachment;
		// This subpass only uses a single color attachment, and does not use any other attachmen types. We can 10
		// therefore leave many of the members at zero/nullptr. If this subpass used a depth attachment (=depth buffer), 11
		// we would specify this via the pDepthStencilAttachment member. 12
		// See the documentation for VkSubpassDescription for other attachment types and the use/meaning of those.

		VkSubpassDependency deps[2]{};
		deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; // == subpasses[0] declared above
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

	void create_swapchain_framebuffers(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, std::vector<lut::Framebuffer>& aFramebuffers, VkImageView aDepthView)
	{
		assert(aFramebuffers.empty());
		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		for (std::size_t i = 0; i < aWindow.swapViews.size(); ++i)
		{
			VkImageView attachments[2] =
			{
				aWindow.swapViews[i],
				aDepthView
			};
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.flags = 0; //normal framebuffer
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

	void record_commands(
		VkCommandBuffer aCmdBuff,
		VkRenderPass aRenderPass,
		VkFramebuffer aFramebuffer,
		VkPipeline aGraphicsPipe,
		VkPipeline aColorizedPipe,
		VkExtent2D const& aImageExtent,
		VkBuffer aSceneUBO,
		glsl::SceneUniform const& aSceneUniform,
		VkPipelineLayout aGraphicsLayout,
		VkDescriptorSet aSceneDescriptors,
		VkDescriptorSet aObjectDecriptors,
		std::vector<TexturedMesh> const& aModelMesh,
		std::vector<ColorizedMesh> const& aColorizedMesh,
		std::vector<VkDescriptorSet> aModelDescriptors
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

		// Begin render pass
		VkClearValue clearValues[2]{};
		clearValues[0].color.float32[0] = 0.1f; // Clear to a dark gray background.
		clearValues[0].color.float32[1] = 0.1f; // If we were debugging, this would potentially
		clearValues[0].color.float32[2] = 0.1f; // help us see whether the render pass took
		clearValues[0].color.float32[3] = 1.f; // place, even if nothing else was drawn.

		clearValues[1].depthStencil.depth = 1.f;
		VkRenderPassBeginInfo passInfo{};
		passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		passInfo.renderPass = aRenderPass;
		passInfo.framebuffer = aFramebuffer;
		passInfo.renderArea.offset = VkOffset2D{ 0, 0 };
		passInfo.renderArea.extent = VkExtent2D{ aImageExtent.width, aImageExtent.height };
		passInfo.clearValueCount = 2;
		passInfo.pClearValues = clearValues;
		vkCmdBeginRenderPass(aCmdBuff, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
		// Begin drawing with our graphics pipeline
		vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);

		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsPipe);
		for (std::size_t i = 0; i < aModelMesh.size(); i++)
		{
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 1, 1, &aModelDescriptors[i], 0, nullptr);
			VkBuffer spriteBuffers[2] = { aModelMesh[i].positions.buffer, aModelMesh[i].texcoords.buffer };
			VkDeviceSize spriteOffsets[2]{};
			vkCmdBindVertexBuffers(aCmdBuff, 0, 2, spriteBuffers, spriteOffsets);
			vkCmdDraw(aCmdBuff, aModelMesh[i].vertexCount, 1, 0, 0);
		}

		vkCmdBindPipeline(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aColorizedPipe);
		for (std::size_t i = 0; i < aColorizedMesh.size(); i++)
		{
			vkCmdBindDescriptorSets(aCmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, aGraphicsLayout, 0, 1, &aSceneDescriptors, 0, nullptr);
			VkBuffer vertexBuffers[] = { aColorizedMesh[i].positions.buffer, aColorizedMesh[i].colors.buffer }; 
			VkDeviceSize offsets[] {0,0};
			vkCmdBindVertexBuffers(aCmdBuff, 0, 2, vertexBuffers, offsets);
			vkCmdDraw(aCmdBuff, aColorizedMesh[i].vertexCount, 1, 0, 0);
		}

		// End the render pass
		vkCmdEndRenderPass(aCmdBuff);
		// End command recording
		if (auto const res = vkEndCommandBuffer(aCmdBuff); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to end recording command buffer\n" "vkEndCommandBuffer() returned %s", lut::to_string(res).c_str());
		}
	}


	void submit_commands(lut::VulkanContext const& aContext, VkCommandBuffer aCmdBuff, VkFence aFence, VkSemaphore aWaitSemaphore, VkSemaphore aSignalSemaphore)
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

	lut::PipelineLayout create_pipeline_layout(lut::VulkanContext const& aContext, VkDescriptorSetLayout aSceneLayout, VkDescriptorSetLayout aObjectLayout)
	{
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

	lut::Pipeline create_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.
#if CURRENT_VERSION == MESHDENSITY_VISUALIZE
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule geom = lut::load_shader_module(aWindow, cfg::kMeshDensityShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);

		VkPipelineShaderStageCreateInfo stages[3]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
		stages[1].module = geom.handle;
		stages[1].pName = "main";
		stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[2].module = frag.handle;
		stages[2].pName = "main";
#else
		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kFragShaderPath);

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert.handle;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag.handle;
		stages[1].pName = "main";
#endif

		VkPipelineDepthStencilStateCreateInfo depthInfo{};
		depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

#if CURRENT_VERSION == OVERDRAW_VISUALIZE
		depthInfo.depthTestEnable = VK_FALSE;
		depthInfo.depthWriteEnable = VK_FALSE;
#else
		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;

		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;
#endif 
		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription vertexInputs[2]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;//position have 3 float
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 2;//uv have 2 float
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		VkVertexInputAttributeDescription vertexAttributes[2]{};
		vertexAttributes[0].binding = 0;		//must match binding above
		vertexAttributes[0].location = 0;		//must match shader;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;//2float:VK_FORMAT_R32G32_SFLOAT
		vertexAttributes[0].offset = 0;
		vertexAttributes[1].binding = 1;		//must match binding above
		vertexAttributes[1].location = 1;		//must match shader;
		vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;//3float:VK_FORMAT_R32G32B32_SFLOAT
		vertexAttributes[1].offset = 0;
		inputInfo.vertexBindingDescriptionCount = 2;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 2;
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
		rasterInfo.lineWidth = 1.f; // required. 
		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		
		// Define blend state
		// We define one blend state per color attachment - this example uses a single color attachment, so we only
		// need one. Right now, we dont do any blending, so we can ignore most of the members.
		VkPipelineColorBlendAttachmentState blendStates[1]{};

#if CURRENT_VERSION == OVERDRAW_VISUALIZE || CURRENT_VERSION == OVERSHADING_VISUALIZE
		blendStates[0].blendEnable = VK_TRUE;
		blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
		blendStates[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates[0].alphaBlendOp = VK_BLEND_OP_ADD;
#else
		blendStates[0].blendEnable = VK_FALSE;		
#endif		
		blendStates[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blendInfo{};
		blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blendInfo.logicOpEnable = VK_FALSE;
		blendInfo.attachmentCount = 1;
		blendInfo.pAttachments = blendStates;
		
		// Create pipeline
		VkGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
#if CURRENT_VERSION == MESHDENSITY_VISUALIZE
		pipeInfo.stageCount = 3; // vertex + fragment stages
#else
		pipeInfo.stageCount = 2; // vertex + fragment stages
#endif
		pipeInfo.pStages = stages;
		pipeInfo.pVertexInputState = &inputInfo;
		pipeInfo.pInputAssemblyState = &assemblyInfo;
		pipeInfo.pTessellationState = nullptr; // no tessellation
		pipeInfo.pViewportState = &viewportInfo;
		pipeInfo.pRasterizationState = &rasterInfo;
		pipeInfo.pMultisampleState = &samplingInfo;
		pipeInfo.pDepthStencilState = nullptr; // no depth or stencil buffers
		pipeInfo.pColorBlendState = &blendInfo;
		pipeInfo.pDepthStencilState = &depthInfo;
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

	lut::Pipeline create_colorized_pipeline(lut::VulkanWindow const& aWindow, VkRenderPass aRenderPass, VkPipelineLayout aPipelineLayout)
	{
		// Load shader modules
		// For this example, we only use the vertex and fragment shaders. Other shader stages (geometry, tessellation)
		// arent used here, and as such we omit them.

		lut::ShaderModule vert = lut::load_shader_module(aWindow, cfg::kColorizedVertShaderPath);
		lut::ShaderModule frag = lut::load_shader_module(aWindow, cfg::kColorizedFragShaderPath);

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

#if CURRENT_VERSION == OVERDRAW_VISUALIZE
		depthInfo.depthTestEnable = VK_FALSE;
		depthInfo.depthWriteEnable = VK_FALSE;
#else
		depthInfo.depthTestEnable = VK_TRUE;
		depthInfo.depthWriteEnable = VK_TRUE;

		depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthInfo.minDepthBounds = 0.f;
		depthInfo.maxDepthBounds = 1.f;
#endif 
		// For now, we dont have any vertex input attributes - the geometry is generated/defined in the vertex shader.
		VkPipelineVertexInputStateCreateInfo inputInfo{};
		inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription vertexInputs[2]{};
		vertexInputs[0].binding = 0;
		vertexInputs[0].stride = sizeof(float) * 3;//position have 3 float
		vertexInputs[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexInputs[1].binding = 1;
		vertexInputs[1].stride = sizeof(float) * 3;//uv have 2 float
		vertexInputs[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[2]{};
		vertexAttributes[0].binding = 0;		//must match binding above
		vertexAttributes[0].location = 0;		//must match shader;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;//2float:VK_FORMAT_R32G32_SFLOAT
		vertexAttributes[0].offset = 0;

		vertexAttributes[1].binding = 1;		//must match binding above
		vertexAttributes[1].location = 1;		//must match shader;
		vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;//3float:VK_FORMAT_R32G32B32_SFLOAT
		vertexAttributes[1].offset = 0;
		inputInfo.vertexBindingDescriptionCount = 2;
		inputInfo.pVertexBindingDescriptions = vertexInputs;
		inputInfo.vertexAttributeDescriptionCount = 2;
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
		rasterInfo.lineWidth = 1.f; // required. 
		// Define multisampling state
		VkPipelineMultisampleStateCreateInfo samplingInfo{};
		samplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		samplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// Define blend state
		// We define one blend state per color attachment - this example uses a single color attachment, so we only
		// need one. Right now, we dont do any blending, so we can ignore most of the members.
		VkPipelineColorBlendAttachmentState blendStates[1]{};

#if CURRENT_VERSION == OVERDRAW_VISUALIZE || CURRENT_VERSION == OVERSHADING_VISUALIZE
		blendStates[0].blendEnable = VK_TRUE;
		blendStates[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates[0].dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		blendStates[0].colorBlendOp = VK_BLEND_OP_ADD;
		blendStates[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendStates[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendStates[0].alphaBlendOp = VK_BLEND_OP_ADD;
#else
		blendStates[0].blendEnable = VK_FALSE;
#endif		
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
		pipeInfo.pDepthStencilState = &depthInfo;
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


	lut::DescriptorSetLayout create_scene_descriptor_layout(lut::VulkanWindow const& aWindow)
	{
		//throw lut::Error( "Not yet implemented" ); //TODO- (Section 3) implement me!
		VkDescriptorSetLayoutBinding bindings[1]{};
		// Input vertex buffer
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
#if CURRENT_VERSION == MESHDENSITY_VISUALIZE
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT;
#else
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
#endif
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

		VkDescriptorSetLayoutBinding bindings[1]{};
		bindings[0].binding = 0; // this must match the shaders
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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

	void update_scene_uniforms(glsl::SceneUniform& aSceneUniforms, std::uint32_t aFramebufferWidth, std::uint32_t aFramebufferHeight, UserState const& aState)
	{
		float const aspect = aFramebufferWidth / aFramebufferHeight;
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
	}


	void present_results(VkQueue aPresentQueue, VkSwapchainKHR aSwapchain, std::uint32_t aImageIndex, VkSemaphore aRenderFinished, bool& aNeedToRecreateSwapchain)
	{
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
	
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = cfg::kDepthFormat;
		imageInfo.extent.width = aWindow.swapchainExtent.width;
		imageInfo.extent.height = aWindow.swapchainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
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

	ColorizedMesh create_color_mesh(labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator, std::vector<float> vertexData, std::vector<glm::vec3> colors)
	{
		// Vertex data
		auto posSize = sizeof(float) * vertexData.size();
		// colors data
		auto colorSize = sizeof(float) * colors.size();

		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		lut::Buffer VertexPosGPU = lut::create_buffer(
			aAllocator,
			posSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			0,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		);
		lut::Buffer VertexColGPU = lut::create_buffer(
			aAllocator,
			colorSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			0,
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
		);
		lut::Buffer posStaging = lut::create_buffer(
			aAllocator,
			posSize,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			0,
			VMA_MEMORY_USAGE_GPU_TO_CPU
		);
		lut::Buffer colStaging = lut::create_buffer(
			aAllocator,
			colorSize,
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
		std::memcpy(posPtr, vertexData.data(), posSize);
		vmaUnmapMemory(aAllocator.allocator, posStaging.allocation);
		void* colPtr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, colStaging.allocation, &colPtr); VK_SUCCESS != res)
		{
			throw lut::Error("Mapping memory for writing\n"
				"vmaMapMemory() returned %s", lut::to_string(res).c_str());
		}
		std::memcpy(colPtr, colors.data(), colorSize);
		vmaUnmapMemory(aAllocator.allocator, colStaging.allocation);

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
		pcopy.size = posSize;
		vkCmdCopyBuffer(uploadCmd, posStaging.buffer, VertexPosGPU.buffer, 1, &pcopy);
		lut::buffer_barrier(uploadCmd,
			VertexPosGPU.buffer,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
		);
		VkBufferCopy ccopy{};
		ccopy.size = colorSize;
		vkCmdCopyBuffer(uploadCmd, colStaging.buffer, VertexColGPU.buffer, 1, &ccopy);
		lut::buffer_barrier(uploadCmd,
			VertexColGPU.buffer,
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

		return ColorizedMesh{
			std::move(VertexPosGPU),
			std::move(VertexColGPU),
			static_cast<unsigned int>(posSize) // two floats per position 4
		};
	}

	TexturedMesh create_model_mesh(labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator, std::vector<float> vertexData, std::vector<float> texCoords)
	{
		// Vertex data
		auto posSize = sizeof(float) * vertexData.size();
		// Texture Coordinate
		auto texSize = sizeof(float) * texCoords.size();
		// DiffuseColor
		// 
		//  1. Create on - GPU buffer
		//	2. Create CPU / host - visible staging buffer
		//	3. Place data into the staging buffer(std::memcpy)
		//	4. Record commands to copy / transfer data from the staging buffer to the final on - GPU buffer
		//	5. Record appropriate buffer barrier for the final on - GPU buffer
		//	6. Submit commands for execution

		//throw lut::Error( "Not yet implemented" ); //TODO: implement me!
		lut::Buffer VertexPosGPU = lut::create_buffer(
			aAllocator,
			posSize,
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
		lut::Buffer posStaging = lut::create_buffer(
			aAllocator,
			posSize,
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
		void* posPtr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, posStaging.allocation, &posPtr); VK_SUCCESS != res)
		{
			throw lut::Error("Mapping memory for writing\n"
				"vmaMapMemory() returned %s", lut::to_string(res).c_str());
		}
		std::memcpy(posPtr, vertexData.data(), posSize);
		vmaUnmapMemory(aAllocator.allocator, posStaging.allocation);
		void* colPtr = nullptr;
		if (auto const res = vmaMapMemory(aAllocator.allocator, colStaging.allocation, &colPtr); VK_SUCCESS != res)
		{
			throw lut::Error("Mapping memory for writing\n"
				"vmaMapMemory() returned %s", lut::to_string(res).c_str());
		}
		std::memcpy(colPtr, texCoords.data(), texSize);
		vmaUnmapMemory(aAllocator.allocator, colStaging.allocation);

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
		pcopy.size = posSize;
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
		return TexturedMesh{
			std::move(VertexPosGPU),
			std::move(VertexColGPU),
			static_cast<std::uint32_t>(vertexData.size()) // now three floats per position 4
		};
	}

}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
