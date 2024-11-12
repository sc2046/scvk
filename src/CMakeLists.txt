add_executable (book2
"main.cpp"  
"app.cpp" "app.h" "descriptors.h"  "pipelines.h" "pipelines.cpp" "buffer.h" "buffer.cpp" "image.h" "image.cpp" "mesh.cpp")

target_link_libraries(book2 glfw)
target_link_libraries(book2 vk-bootstrap)
target_link_libraries(book2 volk)
target_link_libraries(book2 Vulkan::Vulkan)
target_link_libraries(book2 fmt)
