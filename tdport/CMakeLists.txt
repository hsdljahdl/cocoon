if ((CMAKE_MAJOR_VERSION LESS 3) OR (CMAKE_VERSION VERSION_LESS "3.10"))
  message(FATAL_ERROR "CMake >= 3.10 is required")
endif()

option(TDNET_ENABLE_INSTALL "Enable installation of the library." ON)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()

if (NOT OPENSSL_FOUND)
  find_package(OpenSSL REQUIRED)
  find_package(ZLIB REQUIRED)
endif()

set(TDNET_SOURCE
        td/net/Socks5.cpp
        td/net/SslCtx.cpp
        td/net/SslStream.cpp
        td/net/TransparentProxy.cpp
        td/net/utils.cpp

        td/net/Socks5.h
        td/net/SslCtx.h
        td/net/SslStream.h
        td/net/TransparentProxy.h
        td/net/utils.h

        td/e2e/Keys.cpp
        td/e2e/MessageEncryption.cpp
        td/e2e/Keys.h
        td/e2e/MessageEncryption.h
)

add_library(tdnet_ported STATIC ${TDNET_SOURCE})
target_include_directories(tdnet_ported PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_include_directories(tdnet_ported SYSTEM PRIVATE $<BUILD_INTERFACE:${OPENSSL_INCLUDE_DIR}>)
target_link_libraries(tdnet_ported PUBLIC tdutils tdactor tdnet ton_crypto_core)
if (NOT EMSCRIPTEN)
  target_link_libraries(tdnet_ported PRIVATE ${OPENSSL_SSL_LIBRARY})
endif()
target_link_libraries(tdnet_ported PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES})

if (WIN32)
  if (MINGW)
    target_link_libraries(tdnet_ported PRIVATE ws2_32 mswsock crypt32)
  else()
    target_link_libraries(tdnet_ported PRIVATE ws2_32 Mswsock Crypt32)
  endif()
endif()

if (TDNET_ENABLE_INSTALL)
  install(TARGETS tdnet_ported EXPORT TdStaticTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )
endif()
