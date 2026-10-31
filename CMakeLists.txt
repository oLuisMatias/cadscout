cmake_minimum_required(VERSION 3.21)

# The version number lives in ONE place: "version-string" in vcpkg.json. The apps and
# build\version.txt all read it from there. To release a new version, change it there.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" CADSCOUT_MANIFEST)
string(JSON CADSCOUT_VERSION GET "${CADSCOUT_MANIFEST}" "version-string")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")

project(CADscout VERSION ${CADSCOUT_VERSION} LANGUAGES CXX)
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${CADSCOUT_VERSION}")

if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Svg)
find_package(OpenCASCADE CONFIG REQUIRED)
find_package(ZLIB REQUIRED)  # reads the zip container of 3MF files

set(CADSCOUT_SOURCES
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/Viewport.cpp
  src/Viewport.h
  src/ViewCube.cpp
  src/ViewCube.h
  src/StepLoader.cpp
  src/ModelCache.cpp
  src/ClipboardImage.cpp
  src/ClipboardImage.h
  src/InfoPanel.cpp
  src/InfoPanel.h
  src/InfoSection.h
  src/Translate.cpp
  src/Translate.h
  src/StepLoader.h
  src/ExtraFormats.cpp
  src/ExtraFormats.h
  src/Controls.h
  src/PlateSettings.h
  src/SettingsDialog.cpp
  src/SettingsDialog.h
  src/AboutDialog.cpp
  src/AboutDialog.h
  src/Support.h
  src/SupportButtons.cpp
  src/SupportButtons.h
  src/Icons.h
  resources/icons.qrc
  resources/legal.qrc
  resources/logo.qrc
)

function(add_cadscout target output_name)
  add_executable(${target} WIN32 ${CADSCOUT_SOURCES})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
  target_include_directories(${target} PRIVATE src)
  target_compile_definitions(${target} PRIVATE CADSCOUT_VERSION="${CADSCOUT_VERSION}")
  target_link_libraries(${target} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Svg
    TKernel TKMath TKG3d TKGeomBase TKBRep TKTopAlgo TKMesh
    TKXSBase TKXCAF TKLCAF TKCAF TKCDF TKDE TKDESTEP TKShHealing
    TKDEIGES TKDESTL TKDEOBJ TKDEVRML TKDEGLTF TKRWMesh
    ZLIB::ZLIB
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /MP)
  endif()
endfunction()

add_cadscout(CADscout CADscoutFull)
add_cadscout(CADscoutLite CADscoutLite)
# Each edition has its own program icon (the .ico files are made from resources/logo/app-icon-*.png).
target_sources(CADscout PRIVATE resources/app-full.rc)
target_sources(CADscoutLite PRIVATE resources/app-lite.rc)
target_compile_definitions(CADscoutLite PRIVATE CADSCOUT_LITE)

# Explorer right-click entry "View with CADscout" (part of the Lite Store package, see msix/AppxManifest.xml). A small DLL
# without Qt; the C++ runtime is linked in, so it needs no extra files.
add_library(CADscoutLiteMenu SHARED src/contextmenu/ContextMenu.cpp src/contextmenu/ContextMenu.def)
set_target_properties(CADscoutLiteMenu PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_link_libraries(CADscoutLiteMenu PRIVATE ole32 shell32 shlwapi runtimeobject)
if(MSVC)
  target_compile_options(CADscoutLiteMenu PRIVATE /utf-8)
endif()

add_executable(make_sample tools/make_sample.cpp)
target_link_libraries(make_sample PRIVATE
  TKernel TKMath TKG3d TKGeomBase TKBRep TKTopAlgo TKPrim TKBO TKMesh TKXSBase TKXCAF TKLCAF TKCAF TKCDF TKDE TKDESTEP TKDEIGES TKDESTL TKRWMesh TKShHealing)

# Qt platform plugin must sit next to each exe in platforms/.
find_file(QWINDOWS_PLUGIN qwindows.dll
  PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms"
  NO_DEFAULT_PATH)
if(QWINDOWS_PLUGIN)
  foreach(viewer_target IN ITEMS CADscout CADscoutLite)
    add_custom_command(TARGET ${viewer_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${viewer_target}>/platforms"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QWINDOWS_PLUGIN}" "$<TARGET_FILE_DIR:${viewer_target}>/platforms/")
  endforeach()

  install(FILES "${QWINDOWS_PLUGIN}" DESTINATION platforms COMPONENT Lite)
  install(FILES "${QWINDOWS_PLUGIN}" DESTINATION platforms COMPONENT Full)
endif()

install(TARGETS CADscoutLite
  RUNTIME_DEPENDENCY_SET CADscoutLiteRuntime
  RUNTIME DESTINATION . COMPONENT Lite
)
install(TARGETS CADscoutLiteMenu RUNTIME DESTINATION . COMPONENT Lite)
install(RUNTIME_DEPENDENCY_SET CADscoutLiteRuntime
  DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
  PRE_EXCLUDE_REGEXES
    "api-ms-.*" "ext-ms-.*"
    ".*azureattestmanager\\.dll" ".*azureattestnormal\\.dll" ".*hvsifiletrust\\.dll"
    ".*pdmutilities\\.dll" ".*wpaxholder\\.dll"
  POST_EXCLUDE_REGEXES
    ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*"
    ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Ww][Oo][Ww]64/.*"
  RUNTIME DESTINATION . COMPONENT Lite
)

install(TARGETS CADscout
  RUNTIME_DEPENDENCY_SET CADscoutFullRuntime
  RUNTIME DESTINATION . COMPONENT Full
)
install(RUNTIME_DEPENDENCY_SET CADscoutFullRuntime
  DIRECTORIES "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
  PRE_EXCLUDE_REGEXES
    "api-ms-.*" "ext-ms-.*"
    ".*azureattestmanager\\.dll" ".*azureattestnormal\\.dll" ".*hvsifiletrust\\.dll"
    ".*pdmutilities\\.dll" ".*wpaxholder\\.dll"
  POST_EXCLUDE_REGEXES
    ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*"
    ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Ww][Oo][Ww]64/.*"
  RUNTIME DESTINATION . COMPONENT Full
)

# License and third-party notices travel with both editions (also embedded in the app, see resources/legal.qrc).
install(FILES LICENSE THIRD-PARTY-NOTICES.txt DESTINATION . COMPONENT Lite)
install(FILES LICENSE THIRD-PARTY-NOTICES.txt DESTINATION . COMPONENT Full)
