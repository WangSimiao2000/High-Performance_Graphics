#include "vulkan_window.hpp"

#include <tuple>
#include <limits>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <unordered_set>

#include <cstdio>
#include <cassert>
#include <vulkan/vulkan_core.h>

#include "error.hpp"
#include "to_string.hpp"
#include "context_helpers.hxx"
#include <iostream>

#include "../cw1/current_Version.hpp"
namespace lut = labutils;

namespace
{
	// The device selection process has changed somewhat w.r.t. the one used 
	// earlier (e.g., with VulkanContext.
	VkPhysicalDevice select_device( VkInstance, VkSurfaceKHR );
	float score_device( VkPhysicalDevice, VkSurfaceKHR );

	std::optional<std::uint32_t> find_queue_family( VkPhysicalDevice, VkQueueFlags, VkSurfaceKHR = VK_NULL_HANDLE );

	VkDevice create_device( 
		VkPhysicalDevice,
		std::vector<std::uint32_t> const& aQueueFamilies,
		std::vector<char const*> const& aEnabledDeviceExtensions = {}
	);

	std::vector<VkSurfaceFormatKHR> get_surface_formats( VkPhysicalDevice, VkSurfaceKHR );
	std::unordered_set<VkPresentModeKHR> get_present_modes( VkPhysicalDevice, VkSurfaceKHR );

	std::tuple<VkSwapchainKHR,VkFormat,VkExtent2D> create_swapchain(
		VkPhysicalDevice,
		VkSurfaceKHR,
		VkDevice,
		GLFWwindow*,
		std::vector<std::uint32_t> const& aQueueFamilyIndices = {},
		VkSwapchainKHR aOldSwapchain = VK_NULL_HANDLE
	);

	void get_swapchain_images( VkDevice, VkSwapchainKHR, std::vector<VkImage>& );
	void create_swapchain_image_views( VkDevice, VkFormat, std::vector<VkImage> const&, std::vector<VkImageView>& );
}

namespace labutils
{
	// VulkanWindow
	VulkanWindow::VulkanWindow() = default;

	VulkanWindow::~VulkanWindow()
	{
		// Device-related objects
		for( auto const view : swapViews )
			vkDestroyImageView( device, view, nullptr );

		if( VK_NULL_HANDLE != swapchain )
			vkDestroySwapchainKHR( device, swapchain, nullptr );

		// Window and related objects
		if( VK_NULL_HANDLE != surface )
			vkDestroySurfaceKHR( instance, surface, nullptr );

		if( window )
		{
			glfwDestroyWindow( window );

			// The following assumes that we never create more than one window;
			// if there are multiple windows, destroying one of them would
			// unload the whole GLFW library. Nevertheless, this solution is
			// convenient when only dealing with one window (which we will do
			// in the exercises), as it ensure that GLFW is unloaded after all
			// window-related resources are.
			glfwTerminate();
		}
	}

	VulkanWindow::VulkanWindow( VulkanWindow&& aOther ) noexcept
		: VulkanContext( std::move(aOther) )
		, window( std::exchange( aOther.window, VK_NULL_HANDLE ) )
		, surface( std::exchange( aOther.surface, VK_NULL_HANDLE ) )
		, presentFamilyIndex( aOther.presentFamilyIndex )
		, presentQueue( std::exchange( aOther.presentQueue, VK_NULL_HANDLE ) )
		, swapchain( std::exchange( aOther.swapchain, VK_NULL_HANDLE ) )
		, swapImages( std::move( aOther.swapImages ) )
		, swapViews( std::move( aOther.swapViews ) )
		, swapchainFormat( aOther.swapchainFormat )
		, swapchainExtent( aOther.swapchainExtent )
	{}

	VulkanWindow& VulkanWindow::operator=( VulkanWindow&& aOther ) noexcept
	{
		VulkanContext::operator=( std::move(aOther) );
		std::swap( window, aOther.window );
		std::swap( surface, aOther.surface );
		std::swap( presentFamilyIndex, aOther.presentFamilyIndex );
		std::swap( presentQueue, aOther.presentQueue );
		std::swap( swapchain, aOther.swapchain );
		std::swap( swapImages, aOther.swapImages );
		std::swap( swapViews, aOther.swapViews );
		std::swap( swapchainFormat, aOther.swapchainFormat );
		std::swap( swapchainExtent, aOther.swapchainExtent );
		return *this;
	}

	// make_vulkan_window()
	VulkanWindow make_vulkan_window()
	{
		VulkanWindow ret;

		// Initialize Volk
		if( auto const res = volkInitialize(); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to load Vulkan API\n" 
				"Volk returned error %s", lut::to_string(res).c_str()
			);
		}

		//TODO-[DONE]: initialize GLFW
		//en: Note: this assumes that we will not create multiple windows that exist concurrently. If multiple windows are o be used, the glfwInit() and the glfwTerminate() (see destructor) calls should be moved elsewhere.
		//zh: 注意：这假设我们不会同时创建多个窗口。如果要使用多个窗口，glfwInit() 和 glfwTerminate()（见析构函数）的调用应该移到其他地方。
		if (GLFW_TRUE != glfwInit())
		{
			char const* errMsg = nullptr;
			glfwGetError(&errMsg);
			throw lut::Error("GLFW initialization failed: %s", errMsg);
		}
		if (!glfwVulkanSupported())
		{
			throw lut::Error("GLFW: Vulkan not supported.");
		}

		// Check for instance layers and extensions
		auto const supportedLayers = detail::get_instance_layers();
		auto const supportedExtensions = detail::get_instance_extensions();

		bool enableDebugUtils = false;

		std::vector<char const*> enabledLayers, enabledExensions;

		//TODO-[DONE]: check that the instance extensions required by GLFW are available,
		//TODO-[DONE]: and if so, request these to be enabled in the instance creation.
		// en: GLFW may require a number of instance extensions
		// zh: GLFW可能需要一些实例扩展
		// en: GLFW returns a bunch of pointers-to-strings; however, GLFW manages these internally, so we must not free them ourselves. GLFW guarantees that the strings remain valid until GLFW terminates
		// zh: GLFW 返回一堆指向字符串的指针；然而，GLFW 在内部管理这些字符串，因此我们不能自己释放它们。GLFW 保证这些字符串在 GLFW 终止之前保持有效。
		std::uint32_t reqExtCount = 0;
		char const** requiredExt = glfwGetRequiredInstanceExtensions(&reqExtCount);
		for (std::uint32_t i = 0; i < reqExtCount; ++i)
		{
			if (!supportedExtensions.count(requiredExt[i]))
			{
				throw lut::Error("GLFW/Vulkan: required instance extension %s not supported", requiredExt[i]);
			}
			enabledExensions.emplace_back(requiredExt[i]);
		}

		// Validation layers support.
#		if !defined(NDEBUG) // debug builds only
		if( supportedLayers.count( "VK_LAYER_KHRONOS_validation" ) )
		{
			enabledLayers.emplace_back( "VK_LAYER_KHRONOS_validation" );
		}

		if( supportedExtensions.count( "VK_EXT_debug_utils" ) )
		{
			enableDebugUtils = true;
			enabledExensions.emplace_back( "VK_EXT_debug_utils" );
		}
#		endif // ~ debug builds

		for( auto const& layer : enabledLayers )
			std::fprintf( stderr, "Enabling layer: %s\n", layer );

		for( auto const& extension : enabledExensions )
			std::fprintf( stderr, "Enabling instance extension: %s\n", extension );

		// Create Vulkan instance
		ret.instance = detail::create_instance( enabledLayers, enabledExensions, enableDebugUtils );

		// Load rest of the Vulkan API
		volkLoadInstance( ret.instance );

		// Setup debug messenger
		if( enableDebugUtils )
			ret.debugMessenger = detail::create_debug_messenger( ret.instance );

		//TODO-[DONE]: create GLFW window
		//TODO-[DONE]: get VkSurfaceKHR from the window
		// en: Create GLFW Window and the Vulkan surface
		// zh: 创建 GLFW 窗口和 Vulkan 表面
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		ret.window = glfwCreateWindow(1280, 720, "Course Work 1 - Simiao Wang", nullptr, nullptr);
		if (!ret.window)
		{
			char const* errMsg = nullptr;
			glfwGetError(&errMsg);
			throw lut::Error("Unable to create GLFW window\n"
				"Last error = %s", errMsg);
		}
		if (auto const res = glfwCreateWindowSurface(ret.instance, ret.window, nullptr, &ret.surface); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create VkSurfaceKHR\n"
				"glfwCreateWindowSurface() returned %s", lut::to_string(res).c_str());
		}

		// Select appropriate Vulkan device
		ret.physicalDevice = select_device( ret.instance, ret.surface );
		if( VK_NULL_HANDLE == ret.physicalDevice )
			throw lut::Error( "No suitable physical device found!" );

		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties( ret.physicalDevice, &props );
			std::fprintf( stderr, "Selected device: %s (%d.%d.%d)\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion) );
		}

		// Create a logical device
		// Enable required extensions. The device selection method ensures that
		// the VK_KHR_swapchain extension is present, so we can safely just
		// request it without further checks.
		std::vector<char const*> enabledDevExensions;

		//TODO-[DONE]: list necessary extensions here
		enabledDevExensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		for( auto const& ext : enabledDevExensions )
			std::fprintf( stderr, "Enabling device extension: %s\n", ext );

		// We need one or two queues:
		// - best case: one GRAPHICS queue that can present
		// - otherwise: one GRAPHICS queue and any queue that can present
		std::vector<std::uint32_t> queueFamilyIndices;

		//TODO-[DONE]: logic to select necessary queue families to instantiate
		if (const auto index = find_queue_family(ret.physicalDevice, VK_QUEUE_GRAPHICS_BIT, ret.surface))
		{
			ret.graphicsFamilyIndex = *index;

			queueFamilyIndices.emplace_back(*index);
		}
		else
		{
			auto graphics = find_queue_family(ret.physicalDevice, VK_QUEUE_GRAPHICS_BIT);
			auto present = find_queue_family(ret.physicalDevice, 0, ret.surface);
			
			assert(graphics && present);

			ret.graphicsFamilyIndex = *graphics;
			ret.presentFamilyIndex = *present;

			queueFamilyIndices.emplace_back(*graphics);
			queueFamilyIndices.emplace_back(*present);
		}

		ret.device = create_device( ret.physicalDevice, queueFamilyIndices, enabledDevExensions );

		// Retrieve VkQueues
		vkGetDeviceQueue( ret.device, ret.graphicsFamilyIndex, 0, &ret.graphicsQueue );

		assert( VK_NULL_HANDLE != ret.graphicsQueue );

		if( queueFamilyIndices.size() >= 2 )
			vkGetDeviceQueue( ret.device, ret.presentFamilyIndex, 0, &ret.presentQueue );
		else
		{
			ret.presentFamilyIndex = ret.graphicsFamilyIndex;
			ret.presentQueue = ret.graphicsQueue;
		}

		// Create swap chain
		std::tie(ret.swapchain, ret.swapchainFormat, ret.swapchainExtent) = create_swapchain( ret.physicalDevice, ret.surface, ret.device, ret.window, queueFamilyIndices );
		
		// Get swap chain images & create associated image views
		get_swapchain_images( ret.device, ret.swapchain, ret.swapImages );
		create_swapchain_image_views( ret.device, ret.swapchainFormat, ret.swapImages, ret.swapViews );

		// Done
		return ret;
	}

	SwapChanges recreate_swapchain( VulkanWindow& aWindow )
	{
		//TODO-[DONE]: implement me!
		// en: Remember old format & extents
		// zh: 记住旧的格式和范围
		// en: These are two of the properties that may change. Typically only the extent changes (e.g., window resized), but the format may in theory also change. If the format changes, we need to recreate additional resources.
		// zh: 这两个属性可能会发生变化。通常只有范围会发生变化（例如，窗口大小调整），但理论上格式也可能会发生变化。如果格式发生变化，我们需要重新创建其他资源。
		const auto oldFormat = aWindow.swapchainFormat;
		const auto oldExtent = aWindow.swapchainExtent;

		// en: Destroy old objects (except for the old swap chain) 
		// zh: 销毁旧对象（除了旧的交换链）
		// en: We keep the old swap chain object around, such that we can pass it to vkCreateSwapchainKHR() via the oldSwapchain member of VkSwapchainCreateInfoKHR.
		// zh: 我们保留旧的交换链对象，以便通过 VkSwapchainCreateInfoKHR 的 oldSwapchain 成员将其传递给 vkCreateSwapchainKHR()。
		VkSwapchainKHR oldSwapChain = aWindow.swapchain;

		for (auto view : aWindow.swapViews)
			vkDestroyImageView(aWindow.device, view, nullptr);

		aWindow.swapViews.clear();
		aWindow.swapImages.clear();

		// en: Create new chain
		// zh: 创建新的交换链
		std::vector<std::uint32_t> queueFamilyIndices;
		if (aWindow.presentFamilyIndex != aWindow.graphicsFamilyIndex)
		{
			queueFamilyIndices.emplace_back(aWindow.graphicsFamilyIndex);
			queueFamilyIndices.emplace_back(aWindow.presentFamilyIndex);
		}
		try
		{
			std::tie(aWindow.swapchain, aWindow.swapchainFormat, aWindow.swapchainExtent) =
				create_swapchain(aWindow.physicalDevice, aWindow.surface, aWindow.device, aWindow.window, queueFamilyIndices, oldSwapChain);
		}
		catch (...)
		{
			// en: Put pack the old swap chain handle into the VulkanWindow; this ensures that the old swap chain is destroyed when this error branch occurs.
			// zh: 将旧的交换链句柄放回 VulkanWindow；这样可以确保在出现错误分支时销毁旧的交换链。
			aWindow.swapchain = oldSwapChain;
			throw;
		}

		// en: Destroy old swap chain
		// zh: 销毁旧的交换链
		vkDestroySwapchainKHR(aWindow.device, oldSwapChain, nullptr);

		// en: Get swap chain images & create associated image views
		// zh: 获取交换链图像并创建相关的图像视图
		get_swapchain_images(aWindow.device, aWindow.swapchain, aWindow.swapImages);
		create_swapchain_image_views(aWindow.device, aWindow.swapchainFormat, aWindow.swapImages, aWindow.swapViews);

		// en: Determine which swap chain properties have changed and return the information indicating this 
		// zh: 确定交换链的哪些属性发生了变化，并返回指示此信息的数据
		SwapChanges ret{};

		if (oldExtent.width != aWindow.swapchainExtent.width || oldExtent.height != aWindow.swapchainExtent.height)
			ret.changedSize = true;
		if (oldFormat != aWindow.swapchainFormat)
			ret.changedFormat = true;

		return ret;
	}
}

namespace
{
	std::vector<VkSurfaceFormatKHR> get_surface_formats( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{
		//TODO-[?]: implement me!
		std::uint32_t numSurfaceFormat = 0;
		if (auto const res = vkGetPhysicalDeviceSurfaceFormatsKHR(aPhysicalDev,
			aSurface, &numSurfaceFormat, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Error: unable to enumerate surface formats\n"
				"vkGetPhysicalDeviceSurfaceFormatsKHR() returned %s", lut::to_string(res).c_str());
		}
		std::vector<VkSurfaceFormatKHR> surfaceFormats(numSurfaceFormat);
		if (auto const res = vkGetPhysicalDeviceSurfaceFormatsKHR(aPhysicalDev,
			aSurface, &numSurfaceFormat, surfaceFormats.data()); VK_SUCCESS != res)
		{
			throw lut::Error("Error: unable to get surface formats\n"
				"vkGetPhysicalDeviceSurfaceFormatsKHR() returned %s", lut::to_string(res).c_str());
		}
		return surfaceFormats;
	}

	std::unordered_set<VkPresentModeKHR> get_present_modes( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{
		//TODO-[?]: implement me!
		std::uint32_t numPresentModes = 0;
		if (auto const res = vkGetPhysicalDeviceSurfacePresentModesKHR(aPhysicalDev,
			aSurface, &numPresentModes, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Error: unable to enumerate present modes\n"
				"vkGetPhysicalDeviceSurfacePresentModesKHR() returned %s", lut::to_string(res).c_str());
		}
		std::vector<VkPresentModeKHR> presentModes(numPresentModes);
		if (auto const res = vkGetPhysicalDeviceSurfacePresentModesKHR(aPhysicalDev,
			aSurface, &numPresentModes, presentModes.data()); VK_SUCCESS != res)
		{
			throw lut::Error("Error: unable to get present modes\n"
				"vkGetPhysicalDeviceSurfacePresentModesKHR() returned %s", lut::to_string(res).c_str());
		}
		std::unordered_set<VkPresentModeKHR> res;
		for (auto const& mode : presentModes)
		{
			res.insert(mode);
		}
		return res;
	}

	std::tuple<VkSwapchainKHR,VkFormat,VkExtent2D> create_swapchain( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface, VkDevice aDevice, GLFWwindow* aWindow, std::vector<std::uint32_t> const& aQueueFamilyIndices, VkSwapchainKHR aOldSwapchain )
	{
		auto const formats = get_surface_formats( aPhysicalDev, aSurface );
		auto const modes = get_present_modes( aPhysicalDev, aSurface );

		//TODO-[DONE]: pick appropriate VkSurfaceFormatKHR format.
		// en: Pick the surface format
		// zh: 选择表面格式
		// en: If there is an 8-bit RGB(A) SRGB format available, pick that. There are two main variations possible here RGBA and BGRA. If neither is available, pick the first one that the driver gives us.
		// zh: 如果有一个可用的 8 位 RGB(A) SRGB 格式，那就选择它。这里有两种主要的变体可能，分别是 RGBA 和 BGRA。如果两者都不可用，则选择驱动程序给出的第一个格式。
		// en: See http://vulkan.gpuinfo.org/listsurfaceformats.php for a list of formats and statistics about where they're supported.
		// zh: 可以参考 http://vulkan.gpuinfo.org/listsurfaceformats.php 查看格式列表和关于它们支持的统计信息。
		assert(!formats.empty());
		VkSurfaceFormatKHR format = formats[0];
		for (auto const fmt : formats)
		{
			if (VK_FORMAT_R8G8B8A8_SRGB == fmt.format && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == fmt.colorSpace)
			{
				format = fmt;
				break;
			}
			if (VK_FORMAT_B8G8R8A8_SRGB == fmt.format && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == fmt.colorSpace)
			{
				format = fmt;
				break;
			}
		}

		//TODO-[DONE]: pick appropriate VkPresentModeKHR
		// en: Pick a presentation mode
		// zh: 选择一个显示模式
		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

		// en: Prefer FIFO RELAXED if it’s available.
		// zh: 如果可用，首选 FIFO RELAXED 模式。
		if (modes.count(VK_PRESENT_MODE_FIFO_RELAXED_KHR))
			presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;

		//TODO-[DONE]: pick image count
		VkSurfaceCapabilitiesKHR caps;
		if (const auto res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(aPhysicalDev, aSurface, &caps); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to get surface capabilities\n" "vkGetPhysicalDeviceSurfaceCapabilitiesKHR() returned %s", lut::to_string(res).c_str());
		}

		std::uint32_t imageCount = 2;
		
		if (imageCount < caps.minImageCount + 1)
			imageCount = caps.minImageCount + 1;

		if (imageCount > 0 && imageCount > caps.maxImageCount)
			imageCount = caps.maxImageCount;

		std::cout << "ImageCount: " << imageCount << std::endl;

		//TODO-[DONE]: figure out swap extent
		VkExtent2D extent = caps.currentExtent;
		if (std::numeric_limits<std::uint32_t>::max() == extent.width)
		{
			int width, height;
			glfwGetFramebufferSize(aWindow, &width, &height);
			// en: Note: we must ensure that the extent is within the range defined by [ minImageExtent, maxImageExtent ]. 
			// zh: 注意：我们必须确保范围在[minImageExtent，maxImageExtent]定义的范围内。
			auto const& min = caps.minImageExtent;
			auto const& max = caps.maxImageExtent;
			extent.width = std::clamp(std::uint32_t(width), min.width, max.width);
			extent.height = std::clamp(std::uint32_t(height), min.height, max.height);
		}

		// TODO-[DONE]: create swap chain
		VkSwapchainCreateInfoKHR chainInfo{};
		chainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		chainInfo.surface = aSurface;
		chainInfo.minImageCount = imageCount;
		chainInfo.imageFormat = format.format;
		chainInfo.imageColorSpace = format.colorSpace;
		chainInfo.imageExtent = extent;
		chainInfo.imageArrayLayers = 1;
		chainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		chainInfo.preTransform = caps.currentTransform;
		chainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		chainInfo.presentMode = presentMode;
		chainInfo.clipped = VK_TRUE;
		chainInfo.oldSwapchain = aOldSwapchain;

		if (aQueueFamilyIndices.size() <= 1)
		{
			chainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}
		else
		{
			// en: Multiple queues may access this resource. There are two options.
			// zh: 多个队列可能访问此资源。有两种选择。
			// en: SHARING MODE CONCURRENT allows access from multiple queues without transferring ownership.
			// zh: 共享模式 CONCURRENT 允许多个队列访问资源而无需转移所有权。
			// en: EXCLUSIVE would require explicit ownership transfers, which we’re avoiding for now.
			// zh: 独占模式 EXCLUSIVE 将需要显式的所有权转移，但目前我们避免使用它。
			// en: EXCLUSIVE may result in better performance than CONCURRENT.
			// zh: 独占模式 EXCLUSIVE 可能比 共享模式 CONCURRENT 更好地提高性能。
			chainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			chainInfo.queueFamilyIndexCount = std::uint32_t(aQueueFamilyIndices.size());
			chainInfo.pQueueFamilyIndices = aQueueFamilyIndices.data();
		}

		VkSwapchainKHR  chain = VK_NULL_HANDLE;
		if (const auto res = vkCreateSwapchainKHR(aDevice, &chainInfo, nullptr, &chain); VK_SUCCESS != res)
		{
			throw lut::Error("Unable to create swap chain\n"
				"vkCreateSwapchainKHR() returned %s", lut::to_string(res).c_str());
		}

		return { chain,format.format,extent };
	}


	void get_swapchain_images( VkDevice aDevice, VkSwapchainKHR aSwapchain, std::vector<VkImage>& aImages )
	{
		assert( 0 == aImages.size() );

		// TODO-[?]: get swapchain image handles with vkGetSwapchainImagesKHR
		std::uint32_t numSwapchainImage = 0;
		if (auto const res = vkGetSwapchainImagesKHR(aDevice, aSwapchain, &numSwapchainImage, nullptr); VK_SUCCESS != res)
		{
			throw lut::Error("Error: Unable to enumerate swapchain images\n" "vkGetSwapchainImagesKHR() returned %s", lut::to_string(res).c_str());
		}
		aImages.resize(numSwapchainImage);
		if (auto const res = vkGetSwapchainImagesKHR(aDevice, aSwapchain, &numSwapchainImage, aImages.data()); VK_SUCCESS != res)
		{
			throw lut::Error("Error: Unable to enumerate swapchain images\n" "vkGetSwapchainImagesKHR() returned %s", lut::to_string(res).c_str());
		}
	}

	void create_swapchain_image_views( VkDevice aDevice, VkFormat aSwapchainFormat, std::vector<VkImage> const& aImages, std::vector<VkImageView>& aViews )
	{
		assert( 0 == aViews.size() );

		// TODO-[?]: create a VkImageView for each of the VkImages.
		for (std::size_t i = 0; i < aImages.size(); i++)
		{
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = aImages[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = aSwapchainFormat;
			viewInfo.components = VkComponentMapping{
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY
			};
			viewInfo.subresourceRange = VkImageSubresourceRange{
				VK_IMAGE_ASPECT_COLOR_BIT,
				0,1,
				0,1
			};
			VkImageView view = VK_NULL_HANDLE;
			if (const auto res = vkCreateImageView(aDevice, &viewInfo, nullptr, &view); VK_SUCCESS != res)
			{
				throw lut::Error("Unable to create image view for swap chain image %zu\n"
					"vkCreateImageView() returned %s", i, lut::to_string(res).c_str());
			}
			aViews.emplace_back(view);
		}
		assert(aViews.size() == aImages.size());
	}
}

namespace
{
	// Note: this finds *any* queue that supports the aQueueFlags. As such,
	// find_queue_family( ..., VK_QUEUE_TRANSFER_BIT, ... );
	// might return a GRAPHICS queue family, since GRAPHICS queues typically
	// also set TRANSFER (and indeed most other operations; GRAPHICS queues are
	// required to support those operations regardless). If you wanted to find
	// a dedicated TRANSFER queue (e.g., such as those that exist on NVIDIA
	// GPUs), you would need to use different logic.
	std::optional<std::uint32_t> find_queue_family( VkPhysicalDevice aPhysicalDev, VkQueueFlags aQueueFlags, VkSurfaceKHR aSurface )
	{
		//TODO-[DONE]: find queue family with the specified queue flags that can 
		//TODO-[DONE]: present to the surface (if specified)
		std::uint32_t numQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(aPhysicalDev, &numQueues, nullptr);

		std::vector<VkQueueFamilyProperties> families(numQueues);
		vkGetPhysicalDeviceQueueFamilyProperties(aPhysicalDev, &numQueues, families.data());

		for (std::uint32_t i = 0; i < numQueues; i++)
		{
			const auto& family = families[i];
			if (aQueueFlags == (aQueueFlags & family.queueFlags))
			{
				if (VK_NULL_HANDLE == aSurface)
					return i;

				VkBool32 supported = VK_FALSE;
				const auto res = vkGetPhysicalDeviceSurfaceSupportKHR(aPhysicalDev, i, aSurface, &supported);

				if (VK_SUCCESS == res && supported)
					return i;
			}
		}

		return {};
	}

	VkDevice create_device( VkPhysicalDevice aPhysicalDev, std::vector<std::uint32_t> const& aQueues, std::vector<char const*> const& aEnabledExtensions )
	{
		if( aQueues.empty() )
			throw lut::Error( "create_device(): no queues requested" );

		float queuePriorities[1] = { 1.f };

		std::vector<VkDeviceQueueCreateInfo> queueInfos( aQueues.size() );
		for( std::size_t i = 0; i < aQueues.size(); ++i )
		{
			auto& queueInfo = queueInfos[i];
			queueInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex  = aQueues[i];
			queueInfo.queueCount        = 1;
			queueInfo.pQueuePriorities  = queuePriorities;
		}

		VkPhysicalDeviceFeatures deviceFeatures{};

		// 各向异性
#if CURRENT_VERSION == ANISOTROPIC_FILTERING
		deviceFeatures.samplerAnisotropy = VK_TRUE;
#elif CURRENT_VERSION == MESHDENSITY_VISUALIZE
		deviceFeatures.geometryShader = VK_TRUE;
#else
		deviceFeatures.samplerAnisotropy = VK_FALSE;
		deviceFeatures.geometryShader = VK_FALSE;
#endif
		
		
		// No extra features for now.
		
		VkDeviceCreateInfo deviceInfo{};
		deviceInfo.sType  = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		deviceInfo.queueCreateInfoCount     = std::uint32_t(queueInfos.size());
		deviceInfo.pQueueCreateInfos        = queueInfos.data();

		deviceInfo.enabledExtensionCount    = std::uint32_t(aEnabledExtensions.size());
		deviceInfo.ppEnabledExtensionNames  = aEnabledExtensions.data();

		deviceInfo.pEnabledFeatures         = &deviceFeatures;

		VkDevice device = VK_NULL_HANDLE;
		if( auto const res = vkCreateDevice( aPhysicalDev, &deviceInfo, nullptr, &device ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to create logical device\n"
				"vkCreateDevice() returned %s", lut::to_string(res).c_str() 
			);
		}

		return device;
	}
}

namespace
{
	float score_device( VkPhysicalDevice aPhysicalDev, VkSurfaceKHR aSurface )
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties( aPhysicalDev, &props );

		// Only consider Vulkan 1.1 devices
		auto const major = VK_API_VERSION_MAJOR( props.apiVersion );
		auto const minor = VK_API_VERSION_MINOR( props.apiVersion );

		if( major < 1 || (major == 1 && minor < 2) )
		{
			std::fprintf( stderr, "Info: Discarding device '%s': insufficient vulkan version\n", props.deviceName );
			return -1.f;
		}

		//TODO-[DONE]: additional checks
		//TODO-[DONE]:  - check that the VK_KHR_swapchain extension is supported
		//TODO-[DONE]:  - check that there is a queue family that can present to the
		//TODO-[DONE]:    given surface
		//TODO-[DONE]:  - check that there is a queue family that supports graphics
		//TODO-[DONE]:    commands

		// en: Check that the device supports the VK KHR swapchain extension
		// zh: 检查设备是否支持 VK_KHR_swapchain 扩展
		auto const exts = lut::detail::get_device_extensions((aPhysicalDev));
		if (!exts.count(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			std::fprintf(stderr, "Info: Discarding device %s: extension %s missing\n", props.deviceName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			return -1.f;
		}
		// en: Ensure there is a queue family that can present to the given surface
		// zh: 确保存在一个可以向给定表面进行呈现的队列族
		if (!find_queue_family(aPhysicalDev, 0, aSurface))
		{
			std::fprintf(stderr, "Info: Discarding device %s: cant present to surface\n", props.deviceName);
			return -1.f;
		}
		// en: Also ensure there is a queue family that supports graphics commands
		// zh: 同时确保存在一个支持图形命令的队列族
		if (!find_queue_family(aPhysicalDev, VK_QUEUE_GRAPHICS_BIT))
		{
			std::fprintf(stderr, "Info: Discarding device %s no graphics queue family\n", props.deviceName);
			return -1.f;
		}

		// Discrete GPU > Integrated GPU > others
		float score = 0.f;

		if( VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType )
			score += 500.f;
		else if( VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU == props.deviceType )
			score += 100.f;

		return score;
	}
	
	VkPhysicalDevice select_device( VkInstance aInstance, VkSurfaceKHR aSurface )
	{
		std::uint32_t numDevices = 0;
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, nullptr ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device count\n"
				"vkEnumeratePhysicalDevices() returned %s", lut::to_string(res).c_str()
			);
		}

		std::vector<VkPhysicalDevice> devices( numDevices, VK_NULL_HANDLE );
		if( auto const res = vkEnumeratePhysicalDevices( aInstance, &numDevices, devices.data() ); VK_SUCCESS != res )
		{
			throw lut::Error( "Unable to get physical device list\n"
				"vkEnumeratePhysicalDevices() returned %s", lut::to_string(res).c_str()
			);
		}

		float bestScore = -1.f;
		VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

		for( auto const device : devices )
		{
			auto const score = score_device( device, aSurface );
			if( score > bestScore )
			{
				bestScore = score;
				bestDevice = device;
			}
		}

		return bestDevice;
	}
}

