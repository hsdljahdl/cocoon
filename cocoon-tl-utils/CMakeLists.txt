set(COCOON_TL_UTILS_SOURCE
  cocoon-tl-utils.hpp
  cocoon-tl-utils.cpp
  cocoon-common-utils.hpp
  parsers.hpp
  parsers.cpp
)

add_library(cocoon-tl-utils STATIC ${COCOON_TL_UTILS_SOURCE})

target_include_directories(cocoon-tl-utils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>)
target_link_libraries(cocoon-tl-utils PUBLIC cocoon_tl_api common tl-utils tl_tonlib_api )
