# Whole-plugin tests: engine (pitch, blend, choke, gate, voices), sample loading, state, stress + editor snapshots.
#   StSamplePluginTests [--snapshot <dir>] [--video <dir>] [--usecases <dir> [scene ...]]
# Links the plugin's shared-code library, so it exercises the real processor.
juce_add_console_app(StSamplePluginTests PRODUCT_NAME "StSamplePluginTests")
target_sources(StSamplePluginTests PRIVATE PluginTests.cpp UseCases.cpp)
target_compile_features(StSamplePluginTests PRIVATE cxx_std_20)
target_link_libraries(StSamplePluginTests PRIVATE STSample)
target_include_directories(StSamplePluginTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Source")
target_include_directories(StSamplePluginTests PRIVATE $<TARGET_PROPERTY:STSample,INCLUDE_DIRECTORIES>)
target_compile_definitions(StSamplePluginTests PRIVATE $<TARGET_PROPERTY:STSample,COMPILE_DEFINITIONS> $<TARGET_PROPERTY:STSample,INTERFACE_COMPILE_DEFINITIONS>)
