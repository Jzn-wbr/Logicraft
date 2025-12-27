cmake_minimum_required(VERSION 3.21)

project(logicraft VERSION 2.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL2 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(logicraft
  src/main.cpp
  src/render.cpp
  src/world.cpp
)

target_link_libraries(logicraft PRIVATE
  SDL2::SDL2
  GLEW::GLEW
  OpenGL::GL
  OpenGL::GLU
)

if(TARGET SDL2::SDL2main)
  target_link_libraries(logicraft PRIVATE SDL2::SDL2main)
endif()

target_include_directories(logicraft PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
  set(LOGICRAFT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/images/logicraft.ico")
  if(EXISTS "${LOGICRAFT_ICON}")
    target_sources(logicraft PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/logicraft.rc")
  endif()
endif()

install(TARGETS logicraft RUNTIME DESTINATION .)
install(FILES config.cfg DESTINATION .)
install(DIRECTORY images DESTINATION .)
install(DIRECTORY maps DESTINATION .)

if(WIN32)
  install(FILES $<TARGET_RUNTIME_DLLS:logicraft> DESTINATION .)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  include(InstallRequiredSystemLibraries)
  install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION .)
endif()

set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "Logicraft")
set(CPACK_PACKAGE_VENDOR "Logicraft")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Logicraft")
set(CPACK_NSIS_DISPLAY_NAME "Logicraft")
set(CPACK_NSIS_PACKAGE_NAME "Logicraft")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_PACKAGE_EXECUTABLES "logicraft" "Logicraft")
set(CPACK_NSIS_MODIFY_PATH OFF)

set(CPACK_NSIS_MENU_LINKS "logicraft.exe" "Logicraft")
set(CPACK_CREATE_DESKTOP_LINKS "logicraft")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
  "CreateShortCut \\\"$DESKTOP\\\\Logicraft.lnk\\\" \\\"$INSTDIR\\\\logicraft.exe\\\""
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
  "Delete \\\"$DESKTOP\\\\Logicraft.lnk\\\""
)

if(WIN32)
  if(EXISTS "${LOGICRAFT_ICON}")
    set(CPACK_NSIS_MUI_ICON "${LOGICRAFT_ICON}")
    set(CPACK_NSIS_MUI_UNIICON "${LOGICRAFT_ICON}")
  endif()
endif()

include(CPack)
