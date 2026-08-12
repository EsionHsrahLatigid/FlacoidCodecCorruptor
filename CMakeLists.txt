cmake_minimum_required(VERSION 3.22)

project(FlacoidCodecCorruptor VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
  enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLACOIDCODECCORRUPTOR_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(FLACOIDCODECCORRUPTOR_BUILD_TESTS "Build the integration tests" ON)
set(EHL_JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout; avoids network when set")
set(FLACOIDCODECCORRUPTOR_ARTIFACT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH "Stable product staging root")

if(FLACOIDCODECCORRUPTOR_BUILD_PLUGIN)
  include(FetchContent)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  if(EHL_JUCE_SOURCE_DIR)
    if(NOT EXISTS "${EHL_JUCE_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR "EHL_JUCE_SOURCE_DIR does not contain JUCE: ${EHL_JUCE_SOURCE_DIR}")
    endif()
    add_subdirectory("${EHL_JUCE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
  else()
    FetchContent_Declare(
      JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.15
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()
  add_subdirectory(modules/juce-ehl-design-module EXCLUDE_FROM_ALL)

  set(FLACOIDCODECCORRUPTOR_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND FLACOIDCODECCORRUPTOR_FORMATS AU)
  endif()

  juce_add_plugin(FlacoidCodecCorruptor
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "EsionHsrahLatigid"
    BUNDLE_ID "jp.ehl.flacoidcodeccorruptor"
    PRODUCT_NAME "FlacoidCodecCorruptor"
    DESCRIPTION "Frame-based predictor and residual codec corruption effect"
    PLUGIN_MANUFACTURER_CODE EHL_
    PLUGIN_CODE FlCC
    FORMATS ${FLACOIDCODECCORRUPTOR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

  juce_generate_juce_header(FlacoidCodecCorruptor)

  target_sources(FlacoidCodecCorruptor PRIVATE
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

  target_compile_definitions(FlacoidCodecCorruptor
    PUBLIC
      JUCE_REPORT_APP_USAGE=0
      JUCE_WEB_BROWSER=0
      JUCE_USE_CURL=0
      JUCE_VST3_CAN_REPLACE_VST2=0)

  target_link_libraries(FlacoidCodecCorruptor
    PRIVATE
      juce::juce_audio_utils
      juce::juce_dsp
      EHL::JuceDesign
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)

  set(ehl_stage_platform "linux-x64")
  if(APPLE)
    set(ehl_stage_platform "macos-arm64")
  elseif(WIN32)
    set(ehl_stage_platform "windows-x64")
  endif()
  set(flacoid_juce_products "${CMAKE_CURRENT_BINARY_DIR}/FlacoidCodecCorruptor_artefacts/$<CONFIG>")
  set(flacoid_staged_products "${FLACOIDCODECCORRUPTOR_ARTIFACT_ROOT}/plugin-release/${ehl_stage_platform}")
  set(flacoid_stage_dependencies FlacoidCodecCorruptor_VST3 FlacoidCodecCorruptor_Standalone)
  set(flacoid_stage_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${flacoid_staged_products}/vst3"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${flacoid_staged_products}/standalone"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${flacoid_staged_products}/au")

  if(APPLE)
    list(APPEND flacoid_stage_dependencies FlacoidCodecCorruptor_AU)
    list(APPEND flacoid_stage_commands
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${flacoid_staged_products}/standalone/flacoidcodeccorruptor_standalone_plugin.app"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${flacoid_staged_products}/au/flacoidcodeccorruptor_au_plugin.component"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${flacoid_juce_products}/VST3/FlacoidCodecCorruptor.vst3" "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeVst3ModuleInfo.py" "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3/Contents/Resources/moduleinfo.json"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${flacoid_juce_products}/Standalone/FlacoidCodecCorruptor.app" "${flacoid_staged_products}/standalone/flacoidcodeccorruptor_standalone_plugin.app"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${flacoid_juce_products}/AU/FlacoidCodecCorruptor.component" "${flacoid_staged_products}/au/flacoidcodeccorruptor_au_plugin.component"
      COMMAND codesign --force --deep --sign - "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND codesign --force --deep --sign - "${flacoid_staged_products}/standalone/flacoidcodeccorruptor_standalone_plugin.app"
      COMMAND codesign --force --deep --sign - "${flacoid_staged_products}/au/flacoidcodeccorruptor_au_plugin.component")
  elseif(WIN32)
    list(APPEND flacoid_stage_commands
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${flacoid_juce_products}/VST3/FlacoidCodecCorruptor.vst3" "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND ${CMAKE_COMMAND} -E copy "${flacoid_juce_products}/Standalone/FlacoidCodecCorruptor.exe" "${flacoid_staged_products}/standalone/flacoidcodeccorruptor_standalone_plugin.exe")
  else()
    list(APPEND flacoid_stage_commands
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${flacoid_juce_products}/VST3/FlacoidCodecCorruptor.vst3" "${flacoid_staged_products}/vst3/flacoidcodeccorruptor_vst3_plugin.vst3"
      COMMAND ${CMAKE_COMMAND} -E copy "${flacoid_juce_products}/Standalone/FlacoidCodecCorruptor" "${flacoid_staged_products}/standalone/flacoidcodeccorruptor_standalone_plugin")
  endif()

  list(APPEND flacoid_stage_commands
    COMMAND ${CMAKE_COMMAND}
      -DPRODUCT_NAME=FlacoidCodecCorruptor
      -DSLUG=flacoidcodeccorruptor
      -DBUNDLE_ID=jp.ehl.flacoidcodeccorruptor
      -DPLUGIN_CODE=FlCC
      -DSTAGE_DIR=${flacoid_staged_products}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteArtifactsManifest.cmake")

  add_custom_target(ehl_stage_products
    ${flacoid_stage_commands}
    DEPENDS ${flacoid_stage_dependencies}
    COMMENT "Staging FlacoidCodecCorruptor products under ${FLACOIDCODECCORRUPTOR_ARTIFACT_ROOT}/plugin-release")


  if(FLACOIDCODECCORRUPTOR_BUILD_TESTS)
    enable_testing()
    add_executable(FlacoidCodecCorruptorIntegrationTests Tests/PluginIntegrationTest.cpp)
    target_include_directories(FlacoidCodecCorruptorIntegrationTests PRIVATE
      Source
      "${CMAKE_CURRENT_BINARY_DIR}/FlacoidCodecCorruptor_artefacts/JuceLibraryCode")
    target_link_libraries(FlacoidCodecCorruptorIntegrationTests PRIVATE
      FlacoidCodecCorruptor
      EHL::JuceDesign
      juce::juce_audio_utils)
    add_dependencies(FlacoidCodecCorruptorIntegrationTests ehl_stage_products)
    add_test(NAME FlacoidCodecCorruptorPluginIntegrationTests COMMAND FlacoidCodecCorruptorIntegrationTests)
    add_test(
      NAME FlacoidCodecCorruptorArtifactChecks
      COMMAND ${CMAKE_COMMAND}
              -DSTAGE_DIR=${flacoid_staged_products}
              -DSLUG=flacoidcodeccorruptor
              -DEXPECT_AU=$<BOOL:${APPLE}>
              -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyArtifacts.cmake)
  endif()
endif()
