#include <volk/volk.h>

#include <string>
#include <vector>
#include <optional>
#include <unordered_set>

#include <cstdio>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "../labutils/to_string.hpp"
namespace lut = labutils;



namespace
{
	std::unordered_set<std::string> get_instance_layers();
	std::unordered_set<std::string> get_instance_extensions();

	// VkInstance create_instance();// old declaration
	VkInstance create_instance(
		std::vector<char const*> const& aEnabledLayers = {},
		std::vector<char const*> const& aEnabledInstanceExtensions = {},
		bool aEnableDebugUtils = false
	);

	void enumerate_devices( VkInstance );

	float score_device( VkPhysicalDevice );
	VkPhysicalDevice select_device( VkInstance );

	VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance);

	VKAPI_ATTR VkBool32 VKAPI_CALL debug_util_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT,
		VkDebugUtilsMessageTypeFlagsEXT,
		VkDebugUtilsMessengerCallbackDataEXT const*,
		void*
	);

	std::optional<std::uint32_t> find_graphics_queue_family(VkPhysicalDevice);
	VkDevice create_device(VkPhysicalDevice, std::uint32_t aQueueFamily);

}

int main()
{
	// Use Volk to load the initial parts of the Vulkan API that are required
	// to create a Vulkan instance. This includes very few functions:
	// - vkGetInstanceProcAddr()
	// - vkCreateInstance()
	// - vkEnumerateInstanceExtensionProperties()
	// - vkEnumerateInstanceLayerProperties()
	// - vkEnumerateInstanceVersion() (added in Vulkan 1.1)
	if( auto const res = volkInitialize(); VK_SUCCESS != res )
	{
		std::fprintf( stderr, "Error: unable to load Vulkan API\n" );
		std::fprintf( stderr, "Volk returned error %s\n", lut::to_string(res).c_str() );
		return 1;
	}

	// We can use vkEnumerateInstanceVersion() to tell us the version of the
	// Vulkan loader. vkEnumerateInstanceVersion() was added in Vulkan 1.1, so
	// it might not be available on systems with particularly old Vulkan
	// loaders. In that case, assume the version is 1.0.x and output this.
	std::uint32_t loaderVersion = VK_MAKE_API_VERSION( 0, 1, 0, 0 );
	if( vkEnumerateInstanceVersion )
	{
		if( auto const res = vkEnumerateInstanceVersion( &loaderVersion ); VK_SUCCESS != res )
		{
			std::fprintf( stderr, "Warning: vkEnumerateInstanceVersion() returned error %s\n", lut::to_string(res).c_str() );
		}
	}

	std::printf( "Vulkan loader version: %d.%d.%d (variant %d)\n", VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion), VK_API_VERSION_PATCH(loaderVersion), VK_API_VERSION_VARIANT(loaderVersion) );
	// 检查实例层和扩展
	auto const supportedLayers = get_instance_layers();
	auto const supportedExtensions = get_instance_extensions();

	bool enableDebugUtils = false;
	std::vector<char const*> enabledLayers, enabledExtensions;

#ifndef NDEBUG // 仅调试构建
	if (supportedLayers.count("VK_LAYER_KHRONOS_validation")) {
		enabledLayers.emplace_back("VK_LAYER_KHRONOS_validation");
	}

	if (supportedExtensions.count("VK_EXT_debug_utils")) {
		enableDebugUtils = true;
		enabledExtensions.emplace_back("VK_EXT_debug_utils");
	}
#endif // 调试构建结束

	// 打印我们启用的层和扩展的名称
	for (auto const& layer : enabledLayers) {
		std::printf("Enabling layer: %s\n", layer);
	}

	for (auto const& extension : enabledExtensions) {
		std::printf("Enabling instance extension: %s\n", extension);
	}


	// Create Vulkan instance
	//VkInstance instance = create_instance(); // old call
	VkInstance instance = create_instance(enabledLayers, enabledExtensions, enableDebugUtils);

	if( VK_NULL_HANDLE == instance )
		return 1;

	// Instruct Volk to load the remainder of the Vulkan API.
	volkLoadInstance( instance );

	// 设置调试信使
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	if (enableDebugUtils) {
		debugMessenger = create_debug_messenger(instance);
	}



	// Print Vulkan devices
	enumerate_devices( instance );

	// Select appropriate Vulkan device
	VkPhysicalDevice physicalDevice = select_device( instance );
	if( VK_NULL_HANDLE == physicalDevice )
	{
		vkDestroyInstance( instance, nullptr );

		std::fprintf( stderr, "Error: no suitable physical device found!\n" );
		return 1;
	}

	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties( physicalDevice, &props );
		std::printf( "Selected device: %s\n", props.deviceName );
	}

	// 创建逻辑设备
	auto const graphicsFamilyIndex = find_graphics_queue_family(physicalDevice);
	if (!graphicsFamilyIndex) {
		vkDestroyInstance(instance, nullptr);
		std::fprintf(stderr, "Error: no graphics queue found\n");
		return 1;
	}

	VkDevice device = create_device(physicalDevice, *graphicsFamilyIndex);
	if (VK_NULL_HANDLE == device) {
		vkDestroyInstance(instance, nullptr);
		return 1;
	}

	// 可选：为此设备专门化 Vulkan 函数
	volkLoadDevice( device );

	// 检索 VkQueue
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	vkGetDeviceQueue(device, *graphicsFamilyIndex, 0, &graphicsQueue);

	assert(VK_NULL_HANDLE != graphicsQueue);

	// Cleanup

	if (VK_NULL_HANDLE != debugMessenger) {
		vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
	}


	if (device != VK_NULL_HANDLE)
	{
		vkDestroyDevice(device, nullptr);
	}

	vkDestroyInstance(instance, nullptr);
	
	return 0;
}

namespace
{
	VkInstance create_instance(
		std::vector<char const*> const& aEnabledLayers,
		std::vector<char const*> const& aEnabledExtensions,
		bool aEnableDebugUtils
	) 
	{
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "COMP5822-exercise1";
		appInfo.applicationVersion = 2023; // 学年 2023/24
		appInfo.apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0); // Vulkan版本 1.3
		
		VkInstanceCreateInfo instanceInfo{};
		instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceInfo.pApplicationInfo = &appInfo;

		instanceInfo.enabledLayerCount = std::uint32_t(aEnabledLayers.size());
		instanceInfo.ppEnabledLayerNames = aEnabledLayers.data();

		instanceInfo.enabledExtensionCount = std::uint32_t(aEnabledExtensions.size());
		instanceInfo.ppEnabledExtensionNames = aEnabledExtensions.data();


		// 如果我们希望在实例创建时接收验证信息，我们需要提供额外的信息给 vkCreateInstance()。这可以通过将 VkDebugUtilsMessengerCreateInfoEXT 的一个实例链接到 VkInstanceCreateInfo 的 pNext 链中来实现。
		// 注意：这与我们通常初始化调试工具时使用的方式完全相同（请参见 create_debug_messenger()）。
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
		if (aEnableDebugUtils) {
			debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debugInfo.pfnUserCallback = &debug_util_callback;
			debugInfo.pUserData = nullptr;

			debugInfo.pNext = instanceInfo.pNext;
			instanceInfo.pNext = &debugInfo;
		}

		VkInstance instance = VK_NULL_HANDLE;

		if(auto const res = vkCreateInstance(&instanceInfo, nullptr, &instance);VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to create Vulkan instance\n"); 
			std::fprintf(stderr, "vkCreateInstance(): %s\n", lut::to_string(res).c_str()); 
			return VK_NULL_HANDLE; 
		}

		return instance;
	}

	std::optional<std::uint32_t> find_graphics_queue_family(VkPhysicalDevice aPhysicalDev) {
		assert(VK_NULL_HANDLE != aPhysicalDev);

		std::uint32_t numQueues = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(aPhysicalDev, &numQueues, nullptr);

		std::vector<VkQueueFamilyProperties> families(numQueues);
		vkGetPhysicalDeviceQueueFamilyProperties(aPhysicalDev, &numQueues, families.data());

		for (std::uint32_t i = 0; i < numQueues; ++i) {
			auto const& family = families[i];

			if (VK_QUEUE_GRAPHICS_BIT & family.queueFlags) {
				return i;
			}
		}

		return {};
	}

	VkDevice create_device(VkPhysicalDevice aPhysicalDev, std::uint32_t aQueueFamily) {
		assert(VK_NULL_HANDLE != aPhysicalDev);

		float queuePriorities[1] = { 1.f };

		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = aQueueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = queuePriorities;

		VkPhysicalDeviceFeatures deviceFeatures{};
		// 现在没有额外的功能。

		VkDeviceCreateInfo deviceInfo{};
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.pEnabledFeatures = &deviceFeatures;

		VkDevice device = VK_NULL_HANDLE;
		if (auto const res = vkCreateDevice(aPhysicalDev, &deviceInfo, nullptr, &device); VK_SUCCESS != res) {
			std::fprintf(stderr, "Error: can’t create logical device\n");
			std::fprintf(stderr, "vkCreateDevice() returned %s\n", lut::to_string(res).c_str());
			return VK_NULL_HANDLE;
		}

		return device;
	}

}

namespace
{
	std::unordered_set<std::string> get_instance_layers()
	{
		std::uint32_t numLayers = 0;
		if (auto const res = vkEnumerateInstanceLayerProperties(&numLayers, nullptr); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to enumerate layers\n");
			std::fprintf(stderr, "vkEnumerateInstanceLayerProperties() returned %s\n", lut::to_string(res).c_str());
			return {};
		}

		std::vector<VkLayerProperties> layers(numLayers);
		if (auto const res = vkEnumerateInstanceLayerProperties(&numLayers, layers.data()); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to get layer properties\n");
			std::fprintf(stderr, "vkEnumerateInstanceLayerProperties() returned %s\n", lut::to_string(res).c_str());
			return {};
		}

		std::unordered_set<std::string> res;
		for (auto const& layer : layers)
			res.insert(layer.layerName);

		return res;
	}

	std::unordered_set<std::string> get_instance_extensions()
	{
		std::uint32_t numExtensions = 0;
		if (auto const res = vkEnumerateInstanceExtensionProperties(nullptr, &numExtensions, nullptr); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to enumerate instance extensions\n");
			std::fprintf(stderr, "vkEnumerateInstanceExtensionProperties() returned %s\n", lut::to_string(res).c_str());
			return {};
		}

		std::vector<VkExtensionProperties> extensions(numExtensions);
		if (auto const res = vkEnumerateInstanceExtensionProperties(nullptr, &numExtensions, extensions.data()); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to get instance extension properties\n");
			std::fprintf(stderr, "vkEnumerateInstanceExtensionProperties() returned %s\n", lut::to_string(res).c_str());
			return {};
		}

		std::unordered_set<std::string> res;
		for (auto const& extension : extensions)
			res.insert(extension.extensionName);

		return res;
	}

	VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance aInstance) {
		assert(VK_NULL_HANDLE != aInstance);

		// 设置其余应用程序的调试消息
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
		debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugInfo.pfnUserCallback = &debug_util_callback;
		debugInfo.pUserData = nullptr;

		VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
		if (auto const res = vkCreateDebugUtilsMessengerEXT(aInstance, &debugInfo, nullptr, &messenger); VK_SUCCESS != res) {
			std::fprintf(stderr, "Error: unable to set up debug messenger\n");
			std::fprintf(stderr, "vkCreateDebugUtilsMessengerEXT() returned %s\n", lut::to_string(res).c_str());
			return VK_NULL_HANDLE;
		}

		return messenger;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL debug_util_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT aSeverity,
		VkDebugUtilsMessageTypeFlagsEXT aType,
		VkDebugUtilsMessengerCallbackDataEXT const* aData,
		void* /*aUserPtr*/
	)
	{
		std::fprintf(stderr, "%s (%s): %s (%d)\n%s\n--\n", lut::to_string(aSeverity).c_str(),
			lut::message_type_flags(aType).c_str(),
			aData->pMessageIdName,
			aData->messageIdNumber,
			aData->pMessage);
		return VK_FALSE;
	}

}

namespace
{
	float score_device( VkPhysicalDevice aPhysicalDev )
	{
		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(aPhysicalDev, &props);

		// Only consider Vulkan 1.2 devices
		auto const major = VK_API_VERSION_MAJOR(props.apiVersion);
		auto const minor = VK_API_VERSION_MINOR(props.apiVersion);

		if (major < 1 || (major == 1 && minor < 2))
		{
			return -1.f;
		}

		float score = 0.f;

		if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType)
		{
			score += 500.f;
		}

		else if (VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU == props.deviceType)
		{
			score += 100.f;
		}

		return score;
	}
	
	VkPhysicalDevice select_device( VkInstance aInstance )
	{	
		assert(VK_NULL_HANDLE != aInstance);

		//std::vector<VkPhysicalDevice> devices; // TODO − get list of physical devices!
		
		// 获取物理设备的数量
		std::uint32_t numDevices = 0;
		if (vkEnumeratePhysicalDevices(aInstance, &numDevices, nullptr) != VK_SUCCESS) {
			// 如果无法获取物理设备数量，返回 VK_NULL_HANDLE
			return VK_NULL_HANDLE;
		}

		// 如果没有物理设备，同样返回 VK_NULL_HANDLE
		if (numDevices == 0) {
			return VK_NULL_HANDLE;
		}

		// 获取所有物理设备的句柄
		std::vector<VkPhysicalDevice> devices(numDevices);
		if (vkEnumeratePhysicalDevices(aInstance, &numDevices, devices.data()) != VK_SUCCESS) {
			// 如果无法枚举物理设备，返回 VK_NULL_HANDLE
			return VK_NULL_HANDLE;
		}

		float bestScore = -1.f;
		VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

		for (auto const device : devices)
		{
			auto const score = score_device(device);
			if (score > bestScore)
			{
				bestScore = score;
				bestDevice = device;
			}
		}

		return bestDevice;
	}

	void enumerate_devices( VkInstance aInstance )
	{
		assert( VK_NULL_HANDLE != aInstance );

		std::uint32_t numDevices = 0;
		if (auto const res = vkEnumeratePhysicalDevices(aInstance, &numDevices, nullptr); VK_SUCCESS != res) 
		{
			std::fprintf(stderr, "Error: unable to get phyiscal device count\n");
			std::fprintf(stderr, "vkEnumeratePhysicalDevices() returned error %s\n", lut::to_string(res).c_str());
			return;
		}

		std::vector<VkPhysicalDevice> devices(numDevices, VK_NULL_HANDLE);
		if (auto const res = vkEnumeratePhysicalDevices(aInstance, &numDevices, devices.data()); VK_SUCCESS != res)
		{
			std::fprintf(stderr, "Error: unable to get phyiscal device list\n");
			std::fprintf(stderr, "vkEnumeratePhysicalDevices() returned error %s\n", lut::to_string(res).c_str());
			return;
		}

		std::printf("Found %zu devices:\n", devices.size());
		for (auto const device : devices) 
		{
			//TODO: more code here, see below. 
			// Retrieve basic information (properties) about this device
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(device, &props);

			auto const versionMajor = VK_API_VERSION_MAJOR(props.apiVersion);
			auto const versionMinor = VK_API_VERSION_MINOR(props.apiVersion);
			auto const versionPatch = VK_API_VERSION_PATCH(props.apiVersion);

			// Note: while the driver version is stored in a std::uint32 t like the Vulkan API version, the encoding of
			// the driver version is not standardized. (Some vendors stick to the Vulkan version coding, others do not.)
			std::printf("- %s (Vulkan: %d.%d.%d, Driver: %s)\n", props.deviceName, versionMajor, versionMinor, versionPatch, lut::driver_version(props.vendorID, props.driverVersion).c_str());
			std::printf(" - Type: %s\n", lut::to_string(props.deviceType).c_str());

			// vkGetPhysicalDeviceFeatures2 is only available on devices with an API version ≥ 1.1. Calling it on
			// “older” devices is an error.
			if (versionMajor > 1 || (versionMajor == 1 && versionMinor >= 1))
			{
				VkPhysicalDeviceFeatures2 features{};
				features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

				vkGetPhysicalDeviceFeatures2(device, &features);
				// See documentation for VkPhysicalDeviceFeatures2 and for the original VkPhysicalDeviceFeatures
				// (which is the ‘features’ member of the new version) for a complete list of optional features.
				//
				// In addition, several extensions define additional vkPhysicalDevice*Features() structures that could
				// be queried as well (assuming the corresponding extension is supported).
				// Anisotropic filtering is nice. You will use it in CW1 on devices that support it.
				std::printf(" - Anisotropic filtering: %s\n", features.features.samplerAnisotropy ? "true" : "false");
			}

			std::uint32_t numQueues = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &numQueues, nullptr);

			std::vector<VkQueueFamilyProperties> families(numQueues);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &numQueues, families.data());

			for (auto const& family : families)
			{
				std::printf(" - Queue family: %s (%u queues)\n", lut::queue_flags(family.queueFlags).c_str(), family.queueCount);
			}

			// Each device has a number of memory types and memory heaps. Heaps correspond to a kind of memory,
			// for example system memory or on-device memory (VRAM). Sometimes, there are a few additional
			// special memory regions. Memory types use memory from one of the heaps. When allocating memory,
			// we select a memory type with the desired properties (e.g., HOST VISIBLE = must be accessible from
			// the CPU).
			VkPhysicalDeviceMemoryProperties mem;
			vkGetPhysicalDeviceMemoryProperties(device, &mem);
			std::printf(" - %u heaps\n", mem.memoryHeapCount);
			for (std::uint32_t i = 0; i < mem.memoryHeapCount; ++i)
			{
				std::printf(" - heap %2u: %6zu MBytes, %s\n", i, std::size_t(mem.memoryHeaps[i].size) / 1024 / 1024, lut::memory_heap_flags(mem.memoryHeaps[i].flags).c_str());
			}

			std::printf(" - %u memory types\n", mem.memoryTypeCount);
			for (std::uint32_t i = 0; i < mem.memoryTypeCount; ++i)
			{
				std::printf(" - type %2u: from heap %2u, %s\n", i, mem.memoryTypes[i].heapIndex, lut::memory_property_flags(mem.memoryTypes[i].propertyFlags).c_str());
			}
		}		
	}
}

//EOF vim:syntax=cpp:foldmethod=marker:ts=4:noexpandtab: 
