file(MAKE_DIRECTORY auto/tl)

set(TL_COCOON_API
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/tl/cocoon_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/tl/cocoon_api.h
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/tl/cocoon_api.hpp

  ${CMAKE_CURRENT_SOURCE_DIR}/auto/tl/cocoon_api_json.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/auto/tl/cocoon_api_json.h
)
set(TL_COCOON_API ${TL_COCOON_API} PARENT_SCOPE)

set(TL_COCOON_GENERATE_COMMON_SOURCE
  generate_common.cpp

  tl_json_converter.cpp
  tl_writer_cpp.cpp
  tl_writer_h.cpp
  tl_writer_hpp.cpp
  tl_writer_td.cpp

  tl_json_converter.h
  tl_writer_cpp.h
  tl_writer_h.h
  tl_writer_hpp.h
  tl_writer_td.h
)

if (NOT CMAKE_CROSSCOMPILING)
  set(COCOON_GENERATE_COMMON_CMD cocoon_generate_common)
  add_executable(cocoon_generate_common ${TL_COCOON_GENERATE_COMMON_SOURCE})
  target_link_libraries(cocoon_generate_common PRIVATE tdtl tdutils)
  add_custom_command(
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND ${COCOON_GENERATE_COMMON_CMD}
    COMMENT "Generate common tl source files"
    DEPENDS cocoon_generate_common scheme/cocoon_api.tlo 
    OUTPUT ${TL_COCOON_API}
  )
  add_custom_target(cocoon_tl_generate_common DEPENDS ${TL_COCOON_API} )
else()
  add_custom_target(cocoon_tl_generate_common)
endif()
