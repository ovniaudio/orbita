cmake_minimum_required(VERSION 3.25)

# This tells cmake we have goodies in the /cmake folder
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include (PamplejuceVersion)

# Modern concise way to add dependencies to your project
include (CPM)

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# Change me!
# This is the internal name of the project and the name of JUCE's shared code target
# Note: This cannot have spaces (it may be 2024, but you can't have it all!)
# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs display)
set(PROJECT_NAME "Orbita")

# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs will display)
# You can also just have it be the same thing as PROJECT_NAME
# You may want to append the major version on the end of this (and PROJECT_NAME) ala:
#   set(PROJECT_NAME "MyPlugin_v${MAJOR_VERSION}")
# Doing so enables major versions to show up in IDEs and DAWs as separate plugins
# allowing you to change parameters and behavior without breaking existing user projects
set(PRODUCT_NAME "ORBIT")

# Change me! Used for the MacOS bundle name and Installers
set(COMPANY_NAME "OVNI")

# Change me! Used for the MacOS bundle identifier (and signing)
set(BUNDLE_ID "com.ovni.orbit")

# Change me! Set the plugin formats you want built
# Valid choices: AAX Unity VST VST3 AU AUv3 Standalone
set(FORMATS AU VST3 Standalone)

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

# For simplicity, the name of the CMake project is also the name of the target
project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# Log build environment info (after project() so compiler info is available)
include(PamplejuceLog)

# JUCE is setup as a submodule in the /JUCE folder
# Locally, you must run `git submodule update --init --recursive` once
# and later `git submodule update --remote --merge` to keep it up to date
# On Github Actions, this is done as a part of actions/checkout
add_subdirectory(JUCE)

# Add CLAP format — DISABLED for ORBITA (shipping AU + VST3 only)
# add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add any other modules you want modules here, before the juce_add_plugin call
# juce_add_module(modules/my_module)

# This adds the melatonin inspector module
add_subdirectory (modules/melatonin_inspector)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Icons for the standalone app
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    # Change me!
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"

    # On MacOS, plugin is copied to /Users/yourname/Library/Audio/Plug-Ins/
    COPY_PLUGIN_AFTER_BUILD TRUE

    # Change me!
    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE Ovni

    # Change me!
    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE Orbt
    FORMATS "${FORMATS}"

    # The name of your final executable
    # This is how it's listed in the DAW
    # This can be different from PROJECT_NAME and can have spaces!
    # You might want to use v${MAJOR_VERSION} here once you go to v2...
    PRODUCT_NAME "${PRODUCT_NAME}")

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

# CLAP disabled for ORBITA (AU + VST3 only)
# clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
#     CLAP_ID "${BUNDLE_ID}"
#     CLAP_FEATURES audio-effect)

# Enable fast math, C++20 and a few other target defaults
include(SharedCodeDefaults)


# Manually list all .h and .cpp files for the plugin
# If you are like me, you'll use globs for your sanity.
# Just ensure you employ CONFIGURE_DEPENDS so the build system picks up changes
# If you want to appease the CMake gods and avoid globs, manually add files like so:
# set(SourceFiles Source/PluginEditor.h Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginProcessor.cpp)
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Adds a BinaryData target for embedding assets into the binary
include(Assets)

# MacOS only: Cleans up folder and target organization on Xcode.
include(XcodePrettify)

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    # JUCE_WEB_BROWSER and JUCE_USE_CURL off by default
    JUCE_WEB_BROWSER=0  # If you set this to 1, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Uncomment if you are paying for a an Indie/Pro license or releasing under GPLv3
    # JUCE_DISPLAY_SPLASH_SCREEN=0

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    # JucePlugin_Name is for some reason doesn't use the nicer PRODUCT_NAME
    PRODUCT_NAME_WITHOUT_VERSION="ORBIT"
)

# Link to any other modules you added (with juce_add_module) here!
# Usually JUCE modules must have PRIVATE visibility
# See https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md#juce_add_module
# However, with Pamplejuce, you'll link modules to SharedCode with INTERFACE visibility
# This allows the JUCE plugin targets and the Tests target to link against it
target_link_libraries(SharedCode
    INTERFACE
    Assets
    melatonin_inspector
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# IPP support DISABLED intencionalmente (v0.1.1): IPP es propietaria y su
# link estatico en el slice x86_64 de macOS es incompatible con JUCE-AGPLv3.
# ORBIT no llama a ninguna funcion IPP (no hay ruta #ifdef PAMPLEJUCE_IPP en
# source/); IPP solo lo usaba JUCE para FFT. Sin este include, las 3
# plataformas compilan JUCE-puro = misma ruta ya shippeada en arm64 + Windows.
# include(PamplejuceIPP)

# Everything related to the tests target
include(Tests)

# --- M0.5: libmysofa + spike de HRTF (configurado en MI repo, no en el submódulo cmake/) ---
CPMAddPackage(
    NAME libmysofa
    GITHUB_REPOSITORY hoene/libmysofa
    GIT_TAG v1.3.4
    OPTIONS
        "BUILD_TESTS OFF"
        "CODE_COVERAGE OFF"
)
target_link_libraries(Tests PRIVATE mysofa-static)
target_include_directories(Tests PRIVATE
    ${libmysofa_SOURCE_DIR}/src/hrtf
    ${libmysofa_BINARY_DIR}/src)
target_compile_definitions(Tests PRIVATE
    HRTF_SOFA_PATH="${libmysofa_SOURCE_DIR}/tests/CIPIC_subject_003_hrir_final_itdInDelayField.sofa"
    ORBITA_REPO_DIR="${CMAKE_SOURCE_DIR}")

# A separate target for Benchmarks (keeps the Tests target fast).
#
# OJO: NO usamos `include(Benchmarks)` (el módulo del submódulo cmake/ vendoreado), porque
# ese módulo llama `catch_discover_tests(Benchmarks)` y registra el target en ctest. Mientras
# Benchmarks no esté compilado, ctest lo lista como "Benchmarks_NOT_BUILT" = falso fallo / ruido
# en el suite. Benchmarks es un target OPCIONAL de performance (microbench de wall-clock),
# no un gate funcional, así que lo definimos acá (repo-owned, no en el submódulo) EXCLUIDO del
# build por defecto y FUERA del auto-discovery de ctest. Sigue siendo compilable a mano:
#   cmake --build build --config Release --target Benchmarks
file(GLOB_RECURSE BenchmarkFiles CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/Catch2Main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.h")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks PREFIX "" FILES ${BenchmarkFiles})

add_executable(Benchmarks EXCLUDE_FROM_ALL ${BenchmarkFiles})
target_compile_features(Benchmarks PRIVATE cxx_std_20)
# A propósito SIN catch_discover_tests(Benchmarks): así no aparece "Benchmarks_NOT_BUILT" en ctest.
target_include_directories(Benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_definitions(Benchmarks PRIVATE $<TARGET_PROPERTY:${PROJECT_NAME},COMPILE_DEFINITIONS>)
target_link_libraries(Benchmarks PRIVATE SharedCode Catch2::Catch2)
set_target_properties(Benchmarks PROPERTIES XCODE_GENERATE_SCHEME ON)
target_compile_definitions(Benchmarks PUBLIC
    JUCE_MODAL_LOOPS_PERMITTED=1 # let us run Message Manager in tests
    RUN_PAMPLEJUCE_TESTS=1 # also run tests in module .cpp files guarded by RUN_PAMPLEJUCE_TESTS
)

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
