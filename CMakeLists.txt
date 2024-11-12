# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

project(
  "VkCRT"
  VERSION 1.0
  LANGUAGES C CXX
)

# ============================================================================
# compile using C++20
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/${name}")
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${name}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>")

find_package(Vulkan REQUIRED)

include_directories(
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/external/glfw/include"
    "${PROJECT_SOURCE_DIR}/external/fmt/include"
    "${PROJECT_SOURCE_DIR}/external/glm/glm"
    "${PROJECT_SOURCE_DIR}/external/stb_image"
    "${PROJECT_SOURCE_DIR}/external/vk-bootstrap"
    "${PROJECT_SOURCE_DIR}/external/volk/include"
    "${PROJECT_SOURCE_DIR}/external/VulkanMemoryAllocator/include"
    ${Vulkan_INCLUDE_DIR}
)


add_subdirectory("external/glfw")  
add_subdirectory("external/fmt")  
add_subdirectory("external/VulkanMemoryAllocator")
add_subdirectory("external/volk")  
add_subdirectory("external/vk-bootstrap")
add_subdirectory("src")


# Specify the path to the glslangValidator executable directly
set(GLSLANG_VALIDATOR_PATH "$ENV{VULKAN_SDK}/Bin/glslangvalidator")

# Use the specified path in the find_program call
find_program(GLSLANG_VALIDATOR NAMES glslangValidator PATHS ${GLSLANG_VALIDATOR_PATH})
#find_program(GLSL_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

if (NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found! Please ensure it is installed and available in the system PATH.")
endif()


file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/shaders/*.comp"
    "${PROJECT_SOURCE_DIR}/shaders/*.glsl"

    )

foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADERS")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
)
