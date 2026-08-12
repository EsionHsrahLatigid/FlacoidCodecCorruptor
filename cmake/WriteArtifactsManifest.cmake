if(NOT DEFINED STAGE_DIR)
    message(FATAL_ERROR "STAGE_DIR is required")
endif()
file(WRITE "${STAGE_DIR}/ARTIFACTS.txt"
"Product: ${PRODUCT_NAME}
Slug: ${SLUG}
Bundle ID: ${BUNDLE_ID}
Plugin Code: ${PLUGIN_CODE}
Staged artifact contract:
- standalone/${SLUG}_standalone_plugin.app or .exe
- vst3/${SLUG}_vst3_plugin.vst3
- au/${SLUG}_au_plugin.component on Apple
")
