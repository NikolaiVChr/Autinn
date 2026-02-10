cmake_minimum_required(VERSION 3.22)

# Check if building with MetaModule SDK
if(DEFINED METAMODULE_SDK_DIR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdint")

    include(${METAMODULE_SDK_DIR}/plugin.cmake)

    project(Autinn VERSION 2.0 DESCRIPTION "Autinn VCV Plugin Modules" LANGUAGES C CXX ASM)

    add_library(Autinn STATIC)

    file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")
    target_sources(Autinn PRIVATE ${SOURCES})

    target_include_directories(Autinn PRIVATE
            "${METAMODULE_SDK_DIR}/rack-interface/include"
            "${METAMODULE_SDK_DIR}/rack-interface/dep/include"
    )

    create_plugin(
        SOURCE_LIB      Autinn
        PLUGIN_NAME     Autinn
        SOURCE_ASSETS   "${CMAKE_CURRENT_LIST_DIR}/res" # assets folder (with png)
        PLUGIN_JSON     "${CMAKE_CURRENT_LIST_DIR}/plugin.json"
        DESTINATION     "${CMAKE_BINARY_DIR}"
    )
else()
    project(Autinn_IDE)
    set(CMAKE_CXX_STANDARD 11)

    # Allow overriding SDK path, but default to your local path if missing
    if(NOT DEFINED RACK_SDK_DIR)
        set(RACK_SDK_DIR "D:/Rack-SDK")
    endif()

    include_directories("${RACK_SDK_DIR}/include")
    include_directories("${RACK_SDK_DIR}/dep/include")
    include_directories("src")

    file(GLOB_RECURSE SOURCES "src/*.cpp")
    add_library(AutinnDummy SHARED ${SOURCES})
endif()