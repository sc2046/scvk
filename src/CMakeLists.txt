add_executable (book2
"main.cpp"  
"app.cpp" "app.h" )

target_link_libraries(book2 glfw)
target_link_libraries(book2 vk-bootstrap)
target_link_libraries(book2 volk)
target_link_libraries(book2 Vulkan::Vulkan)
target_link_libraries(book2 fmt)

add_custom_command(
	TARGET book2 POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_directory #recursively copy
	${CMAKE_SOURCE_DIR}/shaders/book2
	$<TARGET_FILE_DIR:book2>/shaders/book2)
set_property(TARGET book2 PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")