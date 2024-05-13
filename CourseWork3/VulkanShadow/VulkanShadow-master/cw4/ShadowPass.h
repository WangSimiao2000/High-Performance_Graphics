#pragma once
#include <functional>
#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../labutils/to_string.hpp"
#include "../labutils/vulkan_context.hpp"

#include "../labutils/angle.hpp"
using namespace labutils::literals;

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/vkimage.hpp"
#include "../labutils/vkobject.hpp"
#include "../labutils/vkbuffer.hpp"
#include "../labutils/allocator.hpp" 


#include "VkUBO.hpp"
#include "RenderProgram.hpp"

namespace Shadow {
    namespace lut = labutils;
    class ShadowPass {
    public:
        ShadowPass(lut::VulkanContext &aContext, lut::Allocator const& aAllocator, int size, int width, int height);

        // which light, for point light only
        void triggerPass(int index, glm::vec3 lightPos, float max_dpeth, VkCommandBuffer const&cBuffer, const std::function<void(VkCommandBuffer const&)> &onDraw);
        
        // create desc set
        void createShadowSet(lut::VulkanContext const& aContext,
                lut::Allocator const& aAllocator,
                lut::DescriptorPool const& dPool
        );

        void configShadowPipeLine(
            lut::VulkanContext const& aContext,
            lut::ShaderModule & vert, 
            lut::ShaderModule & frag
        );

        // shadow texture descriptors
        lut::DescriptorSetLayout shadow_set_layout;
        VkDescriptorSet shadow_set;
        
        lut::Sampler shadow_sampler;

    private:
        struct UShadowTransform {
            glm::mat4 mat;
            glm::vec3 light_pos;
            float max_depth;
        };
        std::unique_ptr<VkUBO<UShadowTransform>> shadow_transform_ubo = nullptr;

        RenderPipeLine m_shadow_pipe = {};

        void createRenderPass(lut::VulkanContext &aContext);
        void createCubeArray(lut::VulkanContext &aContext, lut::Allocator const& aAllocator);
        void createFrameBuffers(lut::VulkanContext &aContext);

        lut::RenderPass m_pass;
        std::vector<lut::Framebuffer> m_fbs;
        std::tuple<lut::ImageView, lut::Image> m_shadow_cube_array;
        std::vector<lut::ImageView> attachemt_views;
        int m_size = 1;
        int m_width = 256;
        int m_height = 256;
    };
}