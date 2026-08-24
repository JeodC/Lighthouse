set(ED ${CMAKE_CURRENT_SOURCE_DIR})
set(LH ${CMAKE_SOURCE_DIR})

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

set(ED_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
execute_process(
    COMMAND ${CMAKE_COMMAND} -DSRC_ROOT=${LH} -DOUT_DIR=${ED_GEN}
            -P ${ED}/cmake/GenTables.cmake
    RESULT_VARIABLE ED_GEN_RESULT
)
if(NOT ED_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "editor: GenTables.cmake failed (${ED_GEN_RESULT})")
endif()

# app sources
file(GLOB_RECURSE ED_APP_SOURCES CONFIGURE_DEPENDS
    "${ED}/src/*.cpp"
    "${ED}/src/importer/*.cpp"
)

file(GLOB ED_N_AUDIO_SOURCES CONFIGURE_DEPENDS "${LH}/src/core1/n_audio/*.c")
file(GLOB ED_ULTRA_AUDIO_SOURCES CONFIGURE_DEPENDS "${LH}/lib/ultralib/audio/*.c")

set(ED_DECOMP_SOURCES
    ${ED_N_AUDIO_SOURCES}
    ${ED_ULTRA_AUDIO_SOURCES}
    ${LH}/src/core1/audio_manager.c
    ${LH}/src/core1/audio_instruments.c
    ${LH}/src/core1/audio_soundplayer.c
    ${LH}/src/core1/musicplayer.c
    ${LH}/src/core1/audio_ambience.c
    ${LH}/src/core2/sfx/instruments.c
    ${LH}/src/core2/timedfuncqueue.c
    ${LH}/src/core2/sfx/trackmanager.c
    ${LH}/src/core1/gu_matrix.c
    ${LH}/src/core1/gu_perspective.c
    ${LH}/src/core1/viewport.c
    ${LH}/lib/ultralib/gu/gu_pc.c
    ${LH}/src/port/OS/libultra.c
    ${LH}/src/core1/ml.c
    ${LH}/src/core1/mlmtx.c
    ${LH}/src/core1/sprite.c
    ${LH}/src/core2/file.c
    ${LH}/src/core2/gccube.c
    ${LH}/src/core2/actor_cubepropsystem.c
    ${LH}/src/core2/nc/cameranodelist.c
    ${LH}/src/core2/nc/cameranode_type1.c
    ${LH}/src/core2/nc/cameranode_type2.c
    ${LH}/src/core2/nc/cameranode_type3.c
    ${LH}/src/core2/nc/cameranode_type4.c
    ${LH}/src/core2/map/mapspecificflags.c
    ${LH}/src/core2/gc/sky.c
    ${LH}/src/core2/vla.c
    ${LH}/src/core2/fla.c
    ${LH}/src/core2/texture_dataaccess.c
    ${LH}/src/core2/anim/anim_defrag.c
    ${LH}/src/core2/anim/animtexturecache.c
    ${LH}/src/core2/model/modelRender.c
    ${LH}/src/core2/model/lighting.c
    ${LH}/src/core2/model/mesh_dataaccess.c
    ${LH}/src/core2/model/meshbounds.c
    ${LH}/src/core2/model/rendernormal.c
    ${LH}/src/core2/model/skinning.c
    ${LH}/src/core2/model/vertex_brightness.c
    ${LH}/src/core2/vtx/alphablend.c
    ${LH}/src/core2/vtx/colorapply.c
    ${LH}/src/core2/vtx/colorintensity.c
    ${LH}/src/core2/vtx/colorset.c
    ${LH}/src/core2/vtx/convert.c
    ${LH}/src/core2/vtx/gclights.c
    ${LH}/src/core2/vtx/list.c
    ${LH}/src/core2/vtx/listutils.c
    ${LH}/src/core2/vtx/meshcount.c
    ${LH}/src/core2/vtx/normalset.c
    ${LH}/src/core2/vtx/positionset.c
    ${LH}/src/core2/vtx/renderstart.c
    ${LH}/src/core2/vtx/scale.c
    ${LH}/src/core2/vtx/transform.c
    ${LH}/src/core2/vtx/uvset.c
)

set(ED_SHARED_PORT_SOURCES
    ${LH}/src/port/Audio/mixer.c
    ${LH}/src/port/Audio/AudioInterface.c
    ${LH}/src/port/Audio/AudioSync.cpp
    ${LH}/src/port/Audio/AudioSoundFont.cpp
    ${LH}/src/port/Resource/Importers/ModelFactory.cpp
    ${LH}/src/port/Resource/Importers/SpriteFactory.cpp
    ${LH}/src/port/Resource/Importers/SoundFactory.cpp
    ${LH}/src/port/Resource/Importers/MusicFactory.cpp
    ${LH}/src/port/FilePicker.cpp
    ${LH}/src/port/Resource/Importers/AnimFactory.cpp
    ${LH}/src/port/Resource/Importers/MapFactory.cpp
    ${LH}/src/port/Resource/Type/Sprite.cpp
    ${LH}/src/port/ResourceHelpers.cpp
    ${LH}/src/port/OS/OS_RCP.cpp
    ${LH}/src/port/OS/OS_AI.cpp
    ${LH}/src/port/OS/OS_Time.cpp
    ${LH}/src/port/OS/OS_Timer.cpp
    ${LH}/src/port/OS/OS_Threads.cpp
    ${LH}/src/port/OS/OS_Mesg.cpp
    ${LH}/src/port/GameVersion/BaseGameVersion.cpp
    ${LH}/src/port/Resource/AssetTrace.cpp
    ${LH}/src/port/Resource/Alt/AltPathPool.cpp
    ${LH}/src/port/Resource/GfxBridge.c
)

add_executable(Lightbulb ${ED_APP_SOURCES})
target_sources(Lightbulb PRIVATE ${ED_DECOMP_SOURCES} ${ED_SHARED_PORT_SOURCES})

set_source_files_properties(${ED_SHARED_PORT_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "NOMINMAX"
    INCLUDE_DIRECTORIES "${LH};${LH}/include;${LH}/lib/ultralib/include;${LH}/src;${LH}/libultraship/include;${LH}/libultraship/include/libultraship;${LH}/libultraship/src"
)

set_source_files_properties(${ED_DECOMP_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "EDITOR=1;VERSION_US=1;_LANGUAGE_C;_USE_MATH_DEFINES;NON_MATCHING=1;NON_EQUIVALENT=1;AVOID_UB=1;N_MICRO=1;ENABLE_RUMBLE=1;CIMGUI_DEFINE_ENUMS_AND_STRUCTS"
    INCLUDE_DIRECTORIES "${LH}/include;${LH}/lib/ultralib/include;${LH}/src;${ED}/include;${LH}/libultraship/include;${LH}/libultraship/include/libultraship;${LH}/libultraship/src"
)

set(ED_MODELH_SOURCES
    ${ED}/src/ModelViewer.cpp
    ${ED}/src/LevelScene.cpp
    ${ED}/src/Panels.cpp
    ${ED}/src/SpriteViewer.cpp
    ${ED}/src/ActorOverrides.cpp
    ${ED}/src/PreviewScene.cpp
    ${ED}/src/DecompShims.cpp
    ${ED}/src/Main.cpp
    ${ED}/src/importer/Archive.cpp
    ${ED}/src/importer/Models.cpp
    ${ED}/src/importer/Setup.cpp
    ${ED}/src/importer/Sprites.cpp
    ${ED}/src/importer/Anims.cpp
    ${ED}/src/importer/Tables.cpp
)
set_source_files_properties(${ED_MODELH_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${ED}/include;${ED}/src;${ED}/src/importer;${ED_GEN};${LH}/include;${LH}/lib/ultralib/include;${LH}/src;${LH}/libultraship/include;${LH}/libultraship/include/libultraship"
)

if(WIN32)
    target_sources(Lightbulb PRIVATE "${ED}/Resource.rc")
endif()

target_compile_definitions(Lightbulb PRIVATE EDITOR=1)
target_include_directories(Lightbulb PRIVATE
    "${ED}/include"
    "${ED_GEN}"
    "${LH}/include"
    "${LH}/src"
    "${LH}/libultraship/include"
    "${LH}/libultraship/include/libultraship"
)

target_link_libraries(Lightbulb PRIVATE
    libultraship
    SDL2::SDL2
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    OpenGL::GL
    $<$<BOOL:${WIN32}>:comdlg32>
    $<$<BOOL:${WIN32}>:shell32>
    $<$<BOOL:${WIN32}>:ole32>
    $<$<BOOL:${WIN32}>:dbghelp>
)

if(WIN32)
    set_target_properties(Lightbulb PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

set_target_properties(Lightbulb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(TARGET Lightbulb PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    install(TARGETS Lightbulb DESTINATION . COMPONENT ${PROJECT_NAME})
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    install(TARGETS Lightbulb DESTINATION ../MacOS COMPONENT ${PROJECT_NAME})
endif()

if(MSVC)
    target_compile_options(Lightbulb PRIVATE /utf-8)
endif()

if(EXISTS "${LH}/lighthouse.o2r")
    add_custom_command(TARGET Lightbulb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LH}/lighthouse.o2r $<TARGET_FILE_DIR:Lightbulb>/lighthouse.o2r
    )
endif()
