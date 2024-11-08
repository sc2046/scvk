add_executable (book2
"main.cpp"  
"app.cpp" "app.h" )

target_link_libraries(book2 glfw)
target_link_libraries(book2 vk-bootstrap)
target_link_libraries(book2 volk)
target_link_libraries(book2 Vulkan::Vulkan)
target_link_libraries(book2 fmt)
