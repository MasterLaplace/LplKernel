/**************************************************************************
 * LplKernel - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under the MIT License.
 * https://opensource.org/license/mit
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is free software: you can use, copy, modify, merge, publish and
 * distribute it under the terms of the MIT License, provided this copyright
 * notice and the permission notice are kept. See the LICENSE file.
 *
 * @file config.h
 * @brief Who LplKernel is, how it was built, what it needs, and where it runs.
 *
 * A copy of the Laplace config.h template (MasterLaplace/.github, templates/config.h)
 * under the KERNEL_ prefix. The version below is the only place it is written.
 *
 * The requirements on LplPlugin, LplAssistant and LplKnowledge apply in a translation
 * unit that sees that repository's headers, the only place the compiler can read its
 * version. The kernel is C and sees none of them: each library checks its own, in its
 * identity unit (src/identity.cpp of libengine, libassistant and libknowledge), which
 * includes this file for that reason and then prints the version it found at boot.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-03-28
 **************************************************************************/

/* clang-format off */
#ifndef KERNEL_CONFIG_H_
    #define KERNEL_CONFIG_H_

/**
 * @name Identity
 *
 * The version is written here and nowhere else: the build, the release workflow
 * and CITATION.cff read it from these three lines.
 * @{
 */
#define KERNEL_NAME "LplKernel"
#define KERNEL_VERSION_MAJOR 0
#define KERNEL_VERSION_MINOR 0
#define KERNEL_VERSION_PATCH 5
/** @} */

/** The shared part, down to the Requirements group: laplace-config v1, from MasterLaplace/.github templates/config.h. */
#define KERNEL_CONFIG_TEMPLATE 1

#ifdef __cplusplus
    #include <cstddef>
    #include <cstdint>
#else
    #include <stddef.h>
    #include <stdint.h>
#endif

#ifndef LAPLACE_CONFIG_UTILS
    #define LAPLACE_CONFIG_UTILS

/**
 * @name Portable macros, defined once per translation unit whichever copies it includes
 * @{
 */
#define LPL_NEED_COMMA struct _
#define LPL_UNUSED(x) (void)(x)

#if defined(__GNUC__) || defined(__clang__)
    #define LPL_ATTRIBUTE(key) __attribute__((key))
    #define LPL_UNUSED_ATTRIBUTE LPL_ATTRIBUTE(unused)
    #define LPL_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define LPL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LPL_ATTRIBUTE(key)
    #define LPL_UNUSED_ATTRIBUTE
    #define LPL_LIKELY(x)   (x)
    #define LPL_UNLIKELY(x) (x)
#endif
/** @} */

/**
 * @name Converting a macro to a string
 * @{
 */
#define LPL_STRINGIFY(x) #x
#define LPL_TOSTRING(x) LPL_STRINGIFY(x)
/** @} */

/** Emits a TODO message during compilation, portably. */
#if defined(_MSC_VER)
    #define LPL_TODO(msg) __pragma(message("TODO: " msg))
#else
    #define LPL_TODO(msg) _Pragma(LPL_STRINGIFY(message ("TODO: " msg)))
#endif

/** Portable null pointer: the C++11 nullptr keyword where it exists. */
#if defined(__cplusplus) && __cplusplus >= 201103L
    #define lpl_nullptr nullptr
#elif !defined(NULL)
    #define lpl_nullptr ((void*)0)
#else
    #define lpl_nullptr NULL
#endif

/** Boolean type and values, for C translation units that did not include <stdbool.h>. */
#if !defined(__bool_true_false_are_defined) && !defined(__cplusplus)
    #define bool _Bool
    #define true 1
    #define false 0
    #define __bool_true_false_are_defined 1
#endif

#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
    ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#elif !defined(__GNUC_PREREQ)
# define __GNUC_PREREQ(maj, min) 0
#endif

/**
 * @name Portable structure packing
 *
 * @code
 * LPL_PACKED(struct MyStruct
 * {
 *     int a;
 *     char b;
 * });
 * @endcode
 * @{
 */
#if defined(_MSC_VER) || defined(_MSVC_LANG)
    #define LPL_PACKED( __Declaration__ ) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
    #define LPL_PACKED_START __pragma(pack(push, 1))
    #define LPL_PACKED_END   __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__GNUG__)
    #define LPL_PACKED( __Declaration__ ) __Declaration__ __attribute__((__packed__))
    #define LPL_PACKED_START _Pragma("pack(1)")
    #define LPL_PACKED_END   _Pragma("pack()")
#else
    #define LPL_PACKED( __Declaration__ ) __Declaration__
    #define LPL_PACKED_START
    #define LPL_PACKED_END
#endif
/** @} */

#endif /* !LAPLACE_CONFIG_UTILS */


/**
 * @brief Identifies the compiler as KERNEL_COMPILER_<name> and KERNEL_COMPILER_STRING.
 *
 * @details Clang and MinGW both define __GNUC__, so they are tested before GCC.
 */
#if defined(_MSC_VER) && !defined(__clang__)
    #define KERNEL_COMPILER_MSVC
    #define KERNEL_COMPILER_STRING "MSVC"
#elif defined(__clang__)
    #define KERNEL_COMPILER_CLANG
    #define KERNEL_COMPILER_STRING "Clang"
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define KERNEL_COMPILER_MINGW
    #define KERNEL_COMPILER_STRING "MinGW"
#elif defined(__CYGWIN__)
    #define KERNEL_COMPILER_CYGWIN
    #define KERNEL_COMPILER_STRING "Cygwin"
#elif defined(__GNUC__) || defined(__GNUG__)
    #define KERNEL_COMPILER_GCC
    #define KERNEL_COMPILER_STRING "GCC"
#else
    #error [Config@Distribution]: This compiler is not known to the Laplace config.h template.
#endif


/**
 * @brief Identifies the target system as KERNEL_SYSTEM_<name> and KERNEL_SYSTEM_STRING.
 *
 * @details The Laplace Kernel is tested first: code compiled for it is compiled for it,
 *          whatever the compiler would otherwise suggest. Android is tested before Linux
 *          because it defines __linux__. The kernel target also defines
 *          KERNEL_MODE_STRING, the real-time or standard suffix.
 */
#if defined(__LPL_KERNEL__) || defined(__is_kernel) || (defined(LPL_TARGET_KERNEL) && LPL_TARGET_KERNEL)

    #define KERNEL_SYSTEM_LAPLACE_KERNEL
    #define KERNEL_SYSTEM_STRING "Laplace Kernel"

    #if defined(LPL_KERNEL_REAL_TIME_MODE)
        #define KERNEL_MODE_STRING " (Real-Time)"
    #else
        #define KERNEL_MODE_STRING " (Standard)"
    #endif

#elif defined(_WIN32) || defined(__WIN32__) || defined(__MINGW32__) || defined(__CYGWIN__)

    #define KERNEL_SYSTEM_WINDOWS
    #define KERNEL_SYSTEM_STRING "Windows"

#elif defined(__ANDROID__)

    #define KERNEL_SYSTEM_ANDROID
    #define KERNEL_SYSTEM_STRING "Android"

#elif defined(__linux__) || defined(__linux) || defined(linux)

    #define KERNEL_SYSTEM_LINUX
    #define KERNEL_SYSTEM_STRING "Linux"

#elif defined(__APPLE__)

    #define KERNEL_SYSTEM_MACOS
    #define KERNEL_SYSTEM_STRING "macOS"

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

    #define KERNEL_SYSTEM_FREEBSD
    #define KERNEL_SYSTEM_STRING "FreeBSD"

#elif defined(__unix) || defined(__unix__)

    #define KERNEL_SYSTEM_UNIX
    #define KERNEL_SYSTEM_STRING "Unix"

#else
    #error [Config@Distribution]: This operating system is not known to the Laplace config.h template.
#endif

#ifndef KERNEL_MODE_STRING
    #define KERNEL_MODE_STRING
#endif


/** Identifies the processor as KERNEL_ARCH_<name> and KERNEL_ARCH_STRING. */
#if defined(__x86_64__) || defined(_M_X64)
    #define KERNEL_ARCH_X64
    #define KERNEL_ARCH_STRING "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define KERNEL_ARCH_ARM64
    #define KERNEL_ARCH_STRING "arm64"
#elif defined(__i386__) || defined(_M_IX86)
    #define KERNEL_ARCH_X86
    #define KERNEL_ARCH_STRING "i686"
#elif defined(__riscv) && (__riscv_xlen == 64)
    #define KERNEL_ARCH_RISCV64
    #define KERNEL_ARCH_STRING "riscv64"
#else
    #define KERNEL_ARCH_UNKNOWN
    #define KERNEL_ARCH_STRING "unknown"
#endif


#ifdef __cplusplus
    #define KERNEL_EXTERN_C extern "C"

    #if __cplusplus >= 202302L
        #define KERNEL_CPP23(_) _
        #define KERNEL_CPP20(_) _
        #define KERNEL_CPP17(_) _
        #define KERNEL_CPP14(_) _
        #define KERNEL_CPP11(_) _
        #define KERNEL_CPP99(_) _
    #elif __cplusplus >= 202002L
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_) _
        #define KERNEL_CPP17(_) _
        #define KERNEL_CPP14(_) _
        #define KERNEL_CPP11(_) _
        #define KERNEL_CPP99(_) _
    #elif __cplusplus >= 201703L
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_)
        #define KERNEL_CPP17(_) _
        #define KERNEL_CPP14(_) _
        #define KERNEL_CPP11(_) _
        #define KERNEL_CPP99(_) _
    #elif __cplusplus >= 201402L
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_)
        #define KERNEL_CPP17(_)
        #define KERNEL_CPP14(_) _
        #define KERNEL_CPP11(_) _
        #define KERNEL_CPP99(_) _
    #elif __cplusplus >= 201103L
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_)
        #define KERNEL_CPP17(_)
        #define KERNEL_CPP14(_)
        #define KERNEL_CPP11(_) _
        #define KERNEL_CPP99(_) _
    #elif __cplusplus >= 199711L
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_)
        #define KERNEL_CPP17(_)
        #define KERNEL_CPP14(_)
        #define KERNEL_CPP11(_)
        #define KERNEL_CPP99(_) _
    #else
        #define KERNEL_CPP23(_)
        #define KERNEL_CPP20(_)
        #define KERNEL_CPP17(_)
        #define KERNEL_CPP14(_)
        #define KERNEL_CPP11(_)
        #define KERNEL_CPP99(_)
    #endif

    /**
     * @brief Keeps its argument only when the C++ standard in use is at least @p version.
     *
     * @code
     * void func() KERNEL_CPP14([[deprecated]]);
     * void func() KERNEL_CPP([[deprecated]], 14);
     * @endcode
     */
    #define KERNEL_CPP(_, version) KERNEL_CPP##version(_)

#else
    #define KERNEL_EXTERN_C extern

    #define KERNEL_CPP23(_)
    #define KERNEL_CPP20(_)
    #define KERNEL_CPP17(_)
    #define KERNEL_CPP14(_)
    #define KERNEL_CPP11(_)
    #define KERNEL_CPP99(_)
    #define KERNEL_CPP(_, version)
#endif

/**
 * @name Portable import / export macros for each module
 *
 * Windows compilers need specific (and different) keywords for export and import, and
 * Visual C++ also needs warning C4251 turned off. GCC 4 and later mark symbols visible
 * with one keyword used for both directions; older GCC cannot hide symbols at all, so
 * everything is exported.
 * @{
 */
#if defined(KERNEL_SYSTEM_WINDOWS)

    #define KERNEL_API_EXPORT KERNEL_EXTERN_C __declspec(dllexport)
    #define KERNEL_API_IMPORT KERNEL_EXTERN_C __declspec(dllimport)

    #ifdef _MSC_VER

        #pragma warning(disable : 4251)

    #endif

#elif defined(__GNUC__) && __GNUC__ >= 4

    #define KERNEL_API_EXPORT KERNEL_EXTERN_C __attribute__ ((__visibility__ ("default")))
    #define KERNEL_API_IMPORT KERNEL_EXTERN_C __attribute__ ((__visibility__ ("default")))

#else

    #define KERNEL_API_EXPORT KERNEL_EXTERN_C
    #define KERNEL_API_IMPORT KERNEL_EXTERN_C

#endif
/** @} */


/**
 * @name Portable entry point
 *
 * Windows GUI programs enter through WinMain, Android through android_main with no
 * main function at all, and macOS through a Unix main that also receives the Apple
 * strings. Every other platform uses the standard main.
 * @{
 */
#ifdef KERNEL_SYSTEM_WINDOWS

    #define KERNEL_GUI_MAIN(hInstance, hPrevInstance, lpCmdLine, nCmdShow) WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
    #define KERNEL_MAIN(ac, av, env) main(int ac, char *av[], char *env[])

#elif defined(KERNEL_SYSTEM_ANDROID)

    #define KERNEL_GUI_MAIN(app) android_main(struct android_app* app)
    #define KERNEL_MAIN

#elif defined(KERNEL_SYSTEM_MACOS)

    #define KERNEL_MAIN(ac, av, env, apple) main(int ac, char *av[], char *env[], char *apple[])

#else

    #define KERNEL_MAIN(ac, av, env) main(int ac, char *av[], char *env[])
#endif
/** @} */

/** KERNEL_DEBUG and KERNEL_DEBUG_STRING, from the usual debug flags (LPL_DEBUG included) and NDEBUG. */
#if (defined(_DEBUG) || defined(DEBUG) || defined(LPL_DEBUG)) && !defined(NDEBUG)

    #define KERNEL_DEBUG
    #define KERNEL_DEBUG_STRING "Debug"

#else
    #define KERNEL_DEBUG_STRING "Release"
#endif

/**
 * @name Portable deprecation markers
 *
 * @code
 * KERNEL_DEPRECATED void func();
 * struct KERNEL_DEPRECATED MyStruct { ... };
 * enum KERNEL_DEPRECATED MyEnum { ... };
 * enum MyEnum {
 *     MyEnum1 = 0,
 *     MyEnum2 KERNEL_DEPRECATED,
 *     MyEnum3
 * };
 * class KERNEL_DEPRECATED MyClass { ... };
 * @endcode
 * @{
 */
#ifdef KERNEL_DISABLE_DEPRECATION

    #define KERNEL_DEPRECATED
    #define KERNEL_DEPRECATED_MSG(message)
    #define KERNEL_DEPRECATED_VMSG(version, message)

#elif defined(__cplusplus) && (__cplusplus >= 201402)

    #define KERNEL_DEPRECATED [[deprecated]]
    #define KERNEL_DEPRECATED_MSG(message) [[deprecated(message)]]
    #define KERNEL_DEPRECATED_VMSG(version, message) [[deprecated("since " # version ". " message)]]

#elif defined(KERNEL_COMPILER_MSVC) && (_MSC_VER >= 1900)

    #define KERNEL_DEPRECATED __declspec(deprecated)
    #define KERNEL_DEPRECATED_MSG(message) __declspec(deprecated(message))
    #define KERNEL_DEPRECATED_VMSG(version, message) __declspec(deprecated("since " # version ". " message))

#elif defined(__GNUC__) && __GNUC_PREREQ(4, 9)

    #define KERNEL_DEPRECATED __attribute__((deprecated))
    #define KERNEL_DEPRECATED_MSG(message) __attribute__((deprecated(message)))
    #define KERNEL_DEPRECATED_VMSG(version, message) __attribute__((deprecated("since " # version ". " message)))

#else

    #define KERNEL_DEPRECATED
    #define KERNEL_DEPRECATED_MSG(message)
    #define KERNEL_DEPRECATED_VMSG(version, message)
#endif
/** @} */

/**
 * @name Version
 *
 * The version packs into one integer the way Vulkan's VK_MAKE_API_VERSION does: 7 bits
 * of major, 10 of minor and 12 of patch. Unlike Vulkan's, the macro has no cast, so it
 * also works inside #if, which is where a repository checks the version of another.
 *
 * @code
 * #if !OTHER_COMPATIBLE_WITH(0, 3, 0)
 *     #error "This needs the other repository at 0.3.0 or a later 0.x"
 * #endif
 * @endcode
 * @{
 */
#define KERNEL_MAKE_VERSION(major, minor, patch) (((major) << 22) | ((minor) << 12) | (patch))

#define KERNEL_VERSION \
        KERNEL_MAKE_VERSION(KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR, \
                                      KERNEL_VERSION_PATCH)

/** At least this version. */
#define KERNEL_PREREQ_VERSION(major, minor, patch) \
        (KERNEL_VERSION >= KERNEL_MAKE_VERSION(major, minor, patch))

/** At least this version, and the same major: a new major is a break, never accepted in silence. */
#define KERNEL_COMPATIBLE_WITH(major, minor, patch) \
        (KERNEL_VERSION_MAJOR == (major) && KERNEL_PREREQ_VERSION(major, minor, patch))

#define KERNEL_VERSION_STRING \
        LPL_TOSTRING(KERNEL_VERSION_MAJOR) "." \
        LPL_TOSTRING(KERNEL_VERSION_MINOR) "." \
        LPL_TOSTRING(KERNEL_VERSION_PATCH)
/** @} */

/**
 * @name Build stamp
 *
 * What the source cannot know: the commit it was built from and the build it went into
 * (a profile and a mode, such as "server.debug"). A build passes them with -D to the one
 * translation unit that prints them, so a new commit does not recompile every file.
 * @{
 */
#ifndef KERNEL_COMMIT
    #define KERNEL_COMMIT "unknown"
#endif

#ifndef KERNEL_BUILD
    #define KERNEL_BUILD "unknown"
#endif
/** @} */

/** Compile-time configuration, one KEY=value per line. */
#define KERNEL_CONFIG_STRING \
        "KERNEL_VERSION=" KERNEL_VERSION_STRING "+" KERNEL_BUILD " " KERNEL_COMMIT "\n" \
        "KERNEL_SYSTEM=" KERNEL_SYSTEM_STRING KERNEL_MODE_STRING "\n" \
        "KERNEL_ARCH=" KERNEL_ARCH_STRING "\n" \
        "KERNEL_COMPILER=" KERNEL_COMPILER_STRING "\n" \
        "KERNEL_DEBUG=" KERNEL_DEBUG_STRING "\n"

/** @name Requirements: what this repository needs, checked by the compiler whatever the build system @{ */
#if defined(__cplusplus) && defined(__has_include)
    #if __has_include(<lpl/core/Platform.hpp>)
        #if !__has_include(<lplplugin/config.h>)
            #error "LplKernel needs LplPlugin 0.2.0 or later, and the LplPlugin found has no lplplugin/config.h: update ../LplPlugin"
        #endif
        #include <lplplugin/config.h>
        #if !LPLPLUGIN_COMPATIBLE_WITH(0, 2, 0)
            #pragma message("found LplPlugin " LPLPLUGIN_VERSION_STRING)
            #if LPLPLUGIN_VERSION_MAJOR != 0
                #error "LplKernel was written for LplPlugin 0.x: read what broke in its CHANGELOG, then adapt"
            #else
                #error "LplKernel needs LplPlugin 0.2.0 or later: update ../LplPlugin"
            #endif
        #endif
    #endif

    #if __has_include(<lpl/infer/Inference.hpp>)
        #if !__has_include(<lplassistant/config.h>)
            #error "LplKernel needs LplAssistant 0.1.0 or later, and the LplAssistant found has no lplassistant/config.h: update ../LplAssistant"
        #endif
        #include <lplassistant/config.h>
        #if !LPLASSISTANT_COMPATIBLE_WITH(0, 1, 0)
            #pragma message("found LplAssistant " LPLASSISTANT_VERSION_STRING)
            #if LPLASSISTANT_VERSION_MAJOR != 0
                #error "LplKernel was written for LplAssistant 0.x: read what broke in its CHANGELOG, then adapt"
            #else
                #error "LplKernel needs LplAssistant 0.1.0 or later: update ../LplAssistant"
            #endif
        #endif
    #endif

    #if __has_include(<lpl/knowledge/KnowledgePack.hpp>)
        #if !__has_include(<lplknowledge/config.h>)
            #error "LplKernel needs LplKnowledge 0.1.0 or later, and the LplKnowledge found has no lplknowledge/config.h: update ../LplKnowledge"
        #endif
        #include <lplknowledge/config.h>
        #if !LPLKNOWLEDGE_COMPATIBLE_WITH(0, 1, 0)
            #pragma message("found LplKnowledge " LPLKNOWLEDGE_VERSION_STRING)
            #if LPLKNOWLEDGE_VERSION_MAJOR != 0
                #error "LplKernel was written for LplKnowledge 0.x: read what broke in its CHANGELOG, then adapt"
            #else
                #error "LplKernel needs LplKnowledge 0.1.0 or later: update ../LplKnowledge"
            #endif
        #endif
    #endif
#endif
/** @} */

#endif /* !KERNEL_CONFIG_H_ */
/* clang-format on */
