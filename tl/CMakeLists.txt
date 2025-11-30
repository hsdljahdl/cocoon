add_subdirectory(generate)
set_source_files_properties(${TL_COCOON_API} PROPERTIES GENERATED TRUE)
add_library(cocoon_tl_api STATIC 
	${TL_COCOON_API}
	)
add_dependencies(cocoon_tl_api cocoon_tl_generate_common)
target_link_libraries(cocoon_tl_api tdutils ton_crypto_core )
target_include_directories(cocoon_tl_api PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../ton>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../ton/tl>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generate>)

