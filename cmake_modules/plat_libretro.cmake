# Libretro core platform configuration
# Builds OpenJKDF2 as a Libretro core (.dll/.so/.dylib)

macro(plat_initialize)
    message(STATUS "Target platform: Libretro Core")

    # Set internal target flags
    set(TARGET_POSIX TRUE)
    set(TARGET_USE_SDL2 FALSE)        # Libretro handles windowing/input
    set(TARGET_USE_OPENGL FALSE)      # For now, use software rendering
    set(TARGET_USE_OPENAL FALSE)      # Libretro handles audio
    set(TARGET_USE_LIBSMACKER TRUE)   # Keep video codec support
    set(TARGET_USE_LIBSMUSHER TRUE)   # Keep music support
    set(TARGET_NO_BLOBS TRUE)         # No embedded blobs in core
    set(TARGET_CAN_JKGM FALSE)        # Disable JKGM in Libretro build

    # Determine binary name based on platform
    if(WIN32)
        set(BIN_NAME "openjkdf2_libretro")
        set(LIB_SUFFIX ".dll")
    elseif(APPLE)
        set(BIN_NAME "openjkdf2_libretro")
        set(LIB_SUFFIX ".dylib")
    else()
        set(BIN_NAME "openjkdf2_libretro")
        set(LIB_SUFFIX ".so")
    endif()

    # Compiler flags
    if(MSVC)
        add_compile_options(/W3)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    else()
        add_compile_options(-Wall -Wextra -Wno-unused-parameter)
        add_compile_options(-fPIC)  # Position independent code for shared library
    endif()

    # Libretro-specific defines
    add_compile_definitions(
        LIBRETRO_BUILD
        ARCH_GL_NO_CONTEXT      # Don't create OpenGL context (Libretro frontend handles it)
        ARCH_AUDIO_NO_INIT      # Don't initialize audio (Libretro frontend handles it)
    )
endmacro()

macro(plat_specific_deps)
    # No additional platform-specific dependencies for Libretro
endmacro()

macro(plat_link_and_package)
    # Build as shared library (Libretro core)
    add_library(${BIN_NAME} SHARED
        ${PROJECT_SOURCE_DIR}/src/Platform/Libretro/libretro_core.c
    )

    # Link against the engine
    target_link_libraries(${BIN_NAME} PRIVATE sith_engine)

    # Set library properties
    set_target_properties(${BIN_NAME} PROPERTIES
        PREFIX ""                           # No "lib" prefix
        SUFFIX "${LIB_SUFFIX}"             # Platform-specific extension
        OUTPUT_NAME "${BIN_NAME}"
    )

    # Export all Libretro API symbols
    if(MSVC)
        # On Windows, export all symbols
        set_target_properties(${BIN_NAME} PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        )
    else()
        # On Unix, use visibility attributes
        target_compile_options(${BIN_NAME} PRIVATE -fvisibility=default)
    endif()

    # Install the core to lib directory
    install(TARGETS ${BIN_NAME}
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION lib
    )

    message(STATUS "Libretro core will be built as: ${BIN_NAME}${LIB_SUFFIX}")
endmacro()

macro(plat_extra_deps)
    # No extra dependencies needed
endmacro()
