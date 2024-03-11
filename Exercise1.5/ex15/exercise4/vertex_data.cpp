#include "vertex_data.hpp"

#include <limits>

#include <cstring> // for std::memcpy()

#include "../labutils/error.hpp"
#include "../labutils/vkutil.hpp"
#include "../labutils/to_string.hpp"
namespace lut = labutils;



ColorizedMesh create_triangle_mesh( labutils::VulkanContext const& aContext, labutils::Allocator const& aAllocator )
{
	// Vertex data
	static float const positions[] = {
		0.0f, -0.8f,
		-0.7f, 0.8f,
		+0.7f, 0.8f
	};
	static float const colors[] = {
		0.f, 0.f, 1.f,
		1.f, 0.f, 0.f,
		0.f, 1.f, 0.f
	};

	throw lut::Error( "Not yet implemented" ); //TODO: implement me!
}

