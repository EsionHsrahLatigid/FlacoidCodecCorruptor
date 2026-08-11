cmake_minimum_required(VERSION 3.22)

project(FlacoidCodecCorruptor VERSION 0.1.0 LANGUAGES C CXX OBJC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLACOIDCODECCORRUPTOR_BUILD_PLUGIN "Build the JUCE plug-in targets" ON)
option(FLACOIDCODECCORRUPTOR_BUILD_TESTS "Build the integration tests" ON)
set(FLACOIDCODECCORRUPTOR_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/artifacts" CACHE PATH
    "Directory for final VST3, AU, and Standalone products")

if(FLACOIDCODECCORRUPTOR_BUILD_PLUGIN)
  include(FetchContent)

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
  else()
    FetchContent_Declare(
      JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.15
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()

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
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
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
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)

  set(flacoid_juce_products "${CMAKE_CURRENT_BINARY_DIR}/FlacoidCodecCorruptor_artefacts/$<CONFIG>")
  set(flacoid_staged_products "${FLACOIDCODECCORRUPTOR_ARTIFACT_DIR}/$<CONFIG>")
  set(flacoid_artifact_dependencies FlacoidCodecCorruptor_VST3 FlacoidCodecCorruptor_Standalone)
  set(flacoid_artifact_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${flacoid_staged_products}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${flacoid_staged_products}/VST3"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${flacoid_staged_products}/Standalone"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${flacoid_juce_products}/VST3" "${flacoid_staged_products}/VST3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${flacoid_juce_products}/Standalone" "${flacoid_staged_products}/Standalone")

  if(TARGET FlacoidCodecCorruptor_AU)
    list(APPEND flacoid_artifact_dependencies FlacoidCodecCorruptor_AU)
    list(APPEND flacoid_artifact_commands
      COMMAND ${CMAKE_COMMAND} -E remove_directory "${flacoid_staged_products}/AU"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${flacoid_juce_products}/AU" "${flacoid_staged_products}/AU")
  endif()

  if(APPLE)
    list(APPEND flacoid_artifact_commands
      COMMAND codesign --force --deep --sign -
              "${flacoid_staged_products}/VST3/FlacoidCodecCorruptor.vst3"
      COMMAND codesign --force --deep --sign -
              "${flacoid_staged_products}/Standalone/FlacoidCodecCorruptor.app"
      COMMAND codesign --force --deep --sign -
              "${flacoid_staged_products}/AU/FlacoidCodecCorruptor.component")
  endif()

  add_custom_target(FlacoidCodecCorruptor_Artifacts
    ${flacoid_artifact_commands}
    DEPENDS ${flacoid_artifact_dependencies}
    COMMENT "Staging FlacoidCodecCorruptor products in ${FLACOIDCODECCORRUPTOR_ARTIFACT_DIR}/$<CONFIG>")

  if(FLACOIDCODECCORRUPTOR_BUILD_TESTS)
    enable_testing()
    add_executable(FlacoidCodecCorruptorIntegrationTests Tests/PluginIntegrationTest.cpp)
    target_include_directories(FlacoidCodecCorruptorIntegrationTests PRIVATE
      Source
      "${CMAKE_CURRENT_BINARY_DIR}/FlacoidCodecCorruptor_artefacts/JuceLibraryCode")
    target_link_libraries(FlacoidCodecCorruptorIntegrationTests PRIVATE
      FlacoidCodecCorruptor
      juce::juce_audio_utils)
    add_dependencies(FlacoidCodecCorruptorIntegrationTests FlacoidCodecCorruptor_Artifacts)
    add_test(NAME FlacoidCodecCorruptorPluginIntegrationTests COMMAND FlacoidCodecCorruptorIntegrationTests)
    add_test(
      NAME FlacoidCodecCorruptorArtifactChecks
      COMMAND ${CMAKE_COMMAND}
              -DFLACOIDCODECCORRUPTOR_STAGED_DIR=${FLACOIDCODECCORRUPTOR_ARTIFACT_DIR}/$<CONFIG>
              -DFLACOIDCODECCORRUPTOR_EXPECT_AU=$<BOOL:${APPLE}>
              -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/VerifyArtifacts.cmake)
  endif()
endif()
