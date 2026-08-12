if(NOT DEFINED STAGE_DIR OR NOT DEFINED SLUG)
    message(FATAL_ERROR "STAGE_DIR and SLUG are required")
endif()

if(APPLE)
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin.app")
elseif(WIN32)
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin.exe")
else()
    set(standalone "${STAGE_DIR}/standalone/${SLUG}_standalone_plugin")
endif()
set(vst3 "${STAGE_DIR}/vst3/${SLUG}_vst3_plugin.vst3")
set(manifest "${STAGE_DIR}/ARTIFACTS.txt")

foreach(path IN ITEMS "${standalone}" "${vst3}" "${manifest}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing staged artifact: ${path}")
    endif()
endforeach()

set(module_info "${vst3}/Contents/Resources/moduleinfo.json")
if(NOT EXISTS "${module_info}")
    message(FATAL_ERROR "Missing staged VST3 moduleinfo.json: ${module_info}")
endif()

if(NOT DEFINED Python3_EXECUTABLE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

execute_process(
    COMMAND ${Python3_EXECUTABLE} -m json.tool "${module_info}"
    RESULT_VARIABLE json_result
    OUTPUT_QUIET
    ERROR_VARIABLE json_error)
if(NOT json_result EQUAL 0)
    message(FATAL_ERROR "Invalid strict JSON in ${module_info}: ${json_error}")
endif()

if(WIN32 AND NOT IS_DIRECTORY "${vst3}")
    message(FATAL_ERROR "Windows VST3 must be staged as a directory/container: ${vst3}")
endif()

if(EXPECT_AU)
    set(au "${STAGE_DIR}/au/${SLUG}_au_plugin.component")
    if(NOT EXISTS "${au}")
        message(FATAL_ERROR "Missing staged AU: ${au}")
    endif()
endif()

if(APPLE)
    set(bundles "${vst3}" "${standalone}")
    if(EXPECT_AU)
        list(APPEND bundles "${au}")
    endif()
    foreach(bundle IN LISTS bundles)
        execute_process(COMMAND codesign --verify --deep --strict "${bundle}" RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Invalid ad-hoc signature for ${bundle}: ${error}")
        endif()
    endforeach()
endif()
