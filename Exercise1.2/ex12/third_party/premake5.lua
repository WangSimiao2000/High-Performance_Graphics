-- Third party projects

includedirs( "volk/include" );
includedirs( "vulkan/include" );
includedirs( "stb/include" );

project( "x-volk" )
	kind "StaticLib"

	location "."

	files( "volk/src/*.c" )
	files( "volk/include/volk/*.h" )

project( "x-vulkan-headers" )
	kind "Utility"

	location "."

	files( "vulkan/include/**.h*" )

project( "x-stb" )
	kind "StaticLib"

	location "."

	files( "stb/src/*.c" )

--EOF
