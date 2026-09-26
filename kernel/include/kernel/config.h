/**************************************************************************
 * LplKernel v0.0.0 - A Simple C Kernel for Laplace
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by MasterLaplace.
 * See the Anti-NN License for more details.
 *
 * @file config.h
 * @brief Compile-Time Configuration Parameters for LplKernel.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-03-28
 **************************************************************************/

// clang-format off
#ifndef KERNEL_CONFIG_H_
    #define KERNEL_CONFIG_H_

#ifdef __cplusplus
    #include <utility>
    #include <type_traits>

    #include <cstddef>
    #include <cstdint>
#else
    #include <stddef.h>
    #include <stdint.h>
#endif


#ifndef LAPLACE_CONFIG_UTILS
    #define LAPLACE_CONFIG_UTILS

/**
 * @name Shared portable macros for various compilers
 * @{
 */
#define LPL_NEED_COMMA struct _
#define LPL_ATTRIBUTE(key) __attribute__((key))
#define LPL_UNUSED_ATTRIBUTE LPL_ATTRIBUTE(unused)
#define LPL_UNUSED(x) (void)(x)
#define LPL_LIKELY(x)   __builtin_expect(!!(x), 1)
#define LPL_UNLIKELY(x) __builtin_expect(!!(x), 0)
/** @} */

/** Emits a TODO message during compilation, portably. */
#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
    #define LPL_TODO(msg) _Pragma(LPL_STRINGIFY(message ("TODO: " msg)))
#else
    #define LPL_TODO(msg) __attribute__((warning("TODO: " msg)))
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

/**
 * @name Converting a macro to a string
 * @{
 */
#define LPL_STRINGIFY(x) #x
#define LPL_TOSTRING(x) LPL_STRINGIFY(x)
/** @} */

#endif /* !LAPLACE_CONFIG_UTILS */


#ifndef KERNEL_DISTRIBUTION_H_
    #define KERNEL_DISTRIBUTION_H_

/** Identifies the compiler as KERNEL_COMPILER_<name> and KERNEL_COMPILER_STRING. */
#if defined(_MSC_VER) || defined(_MSVC_LANG)
    #define KERNEL_COMPILER_MSVC
    #define KERNEL_COMPILER_STRING "MSVC"
#elif defined(__GNUC__) || defined(__GNUG__)
    #define KERNEL_COMPILER_GCC
    #define KERNEL_COMPILER_STRING "GCC"
#elif defined(__clang__) || defined(__llvm__)
    #define KERNEL_COMPILER_CLANG
    #define KERNEL_COMPILER_STRING "Clang"
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define KERNEL_COMPILER_MINGW
    #define KERNEL_COMPILER_STRING "MinGW"
#elif defined(__CYGWIN__)
    #define KERNEL_COMPILER_CYGWIN
    #define KERNEL_COMPILER_STRING "Cygwin"
#else
    #error [Config@Distribution]: This compiler is not supported by LplKernel.
#endif


/**
 * @brief Identifies the target system as KERNEL_SYSTEM_<name> and KERNEL_SYSTEM_STRING.
 *
 * @details Android is tested before Linux because it is based on the Linux kernel. The
 *          kernel target also defines KERNEL_MODE_STRING, the real-time or standard suffix.
 */
#if defined(_WIN32) || defined(__WIN32__) || defined(KERNEL_COMPILER_MINGW) || defined(KERNEL_COMPILER_CYGWIN)

    #define KERNEL_SYSTEM_WINDOWS
    #define KERNEL_SYSTEM_STRING "Windows"

#elif defined(__ANDROID__)

    #define KERNEL_SYSTEM_ANDROID
    #define KERNEL_SYSTEM_STRING "Android"

#elif defined(linux) || defined(__linux)

    #define KERNEL_SYSTEM_LINUX
    #define KERNEL_SYSTEM_STRING "Linux"

#elif defined(__unix) || defined(__unix__)

    #define KERNEL_SYSTEM_UNIX
    #define KERNEL_SYSTEM_STRING "Unix"

#elif defined(__APPLE__) || defined(MACOSX) || defined(macintosh) || defined(Macintosh)

    #define KERNEL_SYSTEM_MACOS
    #define KERNEL_SYSTEM_STRING "MacOS"

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

    #define KERNEL_SYSTEM_FREEBSD
    #define KERNEL_SYSTEM_STRING "FreeBSD"

#elif defined(__LPL_KERNEL__) || defined(__is_kernel)

    #define KERNEL_SYSTEM_KERNEL
    #define KERNEL_SYSTEM_STRING "Laplace Kernel"

    #if defined(LPL_KERNEL_REAL_TIME_MODE)
        #define KERNEL_MODE_STRING " (Real-Time)"
    #else
        #define KERNEL_MODE_STRING " (Standard)"
    #endif

#else
    #error [Config@Distribution]: This operating system is not supported by LplKernel.
#endif

#ifndef KERNEL_MODE_STRING
#define KERNEL_MODE_STRING
#endif

#ifdef __cplusplus
    #define KERNEL_EXTERN_C extern "C"

    #if __cplusplus >= 202203L
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

    #define KERNEL_API_EXPORT extern "C" __declspec(dllexport)
    #define KERNEL_API_IMPORT KERNEL_EXTERN_C __declspec(dllimport)

    #ifdef _MSC_VER

        #pragma warning(disable : 4251)

    #endif

#else // Linux, FreeBSD, Mac OS X

    #if __GNUC__ >= 4

        #define KERNEL_API_EXPORT extern "C" __attribute__ ((__visibility__ ("default")))
        #define KERNEL_API_IMPORT KERNEL_EXTERN_C __attribute__ ((__visibility__ ("default")))

    #else

        #define KERNEL_API_EXPORT extern "C"
        #define KERNEL_API_IMPORT KERNEL_EXTERN_C

    #endif

#endif
/** @} */


/**
 * @name Portable entry point
 *
 * Windows GUI programs enter through WinMain, Android through android_main with no
 * main function at all, and MacOS X through a Unix main that also receives the Apple
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

/** KERNEL_DEBUG and KERNEL_DEBUG_STRING, from the usual debug and release flags. */
#if (defined(_DEBUG) || defined(DEBUG)) && !defined(NDEBUG)

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

    #if (__cplusplus >= 201402) && (__cplusplus < 201703)

        #define KERNEL_DEPRECATED_MSG(message) [[deprecated(message)]]
        #define KERNEL_DEPRECATED_VMSG(version, message) \
            [[deprecated("since " # version ". " message)]]

    #else
        #define KERNEL_DEPRECATED_MSG(message) [[deprecated]]
        #define KERNEL_DEPRECATED_VMSG(version, message) [[deprecated]]
    #endif

#elif defined(KERNEL_COMPILER_MSVC) && (_MSC_VER >= 1400)

    #define KERNEL_DEPRECATED __declspec(deprecated)

    #if (_MSC_VER >= 1900)

        #define KERNEL_DEPRECATED_MSG(message) __declspec(deprecated(message))
        #define KERNEL_DEPRECATED_VMSG(version, message) \
            __declspec(deprecated("since " # version ". " message))

    #else
        #define KERNEL_DEPRECATED_MSG(message) __declspec(deprecated)
        #define KERNEL_DEPRECATED_VMSG(version, message) __declspec(deprecated)
    #endif

#elif defined(KERNEL_COMPILER_CLANG) && defined(__has_feature)

    #define KERNEL_DEPRECATED __attribute__((deprecated))

    #if __has_feature(attribute_deprecated_with_message)

        #define KERNEL_DEPRECATED_MSG(message) __attribute__((deprecated(message)))
        #define KERNEL_DEPRECATED_VMSG(version, message) \
            __attribute__((deprecated("since " # version ". " message)))

    #else
        #define KERNEL_DEPRECATED_MSG(message) __attribute__((deprecated))
        #define KERNEL_DEPRECATED_VMSG(version, message) __attribute__((deprecated))
    #endif

#elif defined(KERNEL_COMPILER_GCC) && defined(__GNUC__) && __GNUC_PREREQ(4, 5)

    #define KERNEL_DEPRECATED __attribute__((deprecated))

    #if defined(KERNEL_COMPILER_GCC) && defined(__GNUC__) && __GNUC_PREREQ(4, 9)

        #define KERNEL_DEPRECATED_MSG(message) __attribute__((deprecated(message)))
        #define KERNEL_DEPRECATED_VMSG(version, message) \
            __attribute__((deprecated("since " # version ". " message)))

    #else
        #define KERNEL_DEPRECATED_MSG(message) __attribute__((deprecated))
        #define KERNEL_DEPRECATED_VMSG(version, message) __attribute__((deprecated))
    #endif

#else

    #pragma message("WARNING: KERNEL_DEPRECATED not supported on this compiler")
    #define KERNEL_DEPRECATED
    #define KERNEL_DEPRECATED_MSG(message)
    #define KERNEL_DEPRECATED_VMSG(version, message)
#endif
/** @} */

#endif /* !KERNEL_DISTRIBUTION_H_ */


#ifndef KERNEL_VERSION_H_
    #define KERNEL_VERSION_H_

/**
 * @name Kernel version
 *
 * Each component comes from the FLAG_VERSION_<component> build flag when it is set.
 * @{
 */
#ifdef FLAG_VERSION_MAJOR
    #define KERNEL_VERSION_MAJOR FLAG_VERSION_MAJOR
#else
    #define KERNEL_VERSION_MAJOR 0
#endif

#ifdef FLAG_VERSION_MINOR
    #define KERNEL_VERSION_MINOR FLAG_VERSION_MINOR
#else
    #define KERNEL_VERSION_MINOR 0
#endif

#ifdef FLAG_VERSION_PATCH
    #define KERNEL_VERSION_PATCH FLAG_VERSION_PATCH
#else
    #define KERNEL_VERSION_PATCH 0
#endif

#ifdef FLAG_VERSION_TWEAK
    #define KERNEL_VERSION_TWEAK FLAG_VERSION_TWEAK
#else
    #define KERNEL_VERSION_TWEAK 4
#endif
/** @} */

/** The version as one comparable integer, MMmmppTT. */
#define KERNEL_VERSION_NUM \
        (KERNEL_VERSION_MAJOR * 1000000 + \
        KERNEL_VERSION_MINOR * 10000 + \
        KERNEL_VERSION_PATCH * 100 + \
        KERNEL_VERSION_TWEAK)

#define KERNEL_PREREQ_VERSION(maj, min, pat) (KERNEL_VERSION_NUM >= (maj * 1000000 + min * 10000 + pat * 100))

/** The version components joined by underscores, for token pasting. */
#define KERNEL_VERSION_CCT KERNEL_VERSION_MAJOR##_##KERNEL_VERSION_MINOR##_##KERNEL_VERSION_PATCH##_##KERNEL_VERSION_TWEAK


/** The version as a dotted string. */
#define KERNEL_VERSION_STRING \
        LPL_TOSTRING(KERNEL_VERSION_MAJOR) "." \
        LPL_TOSTRING(KERNEL_VERSION_MINOR) "." \
        LPL_TOSTRING(KERNEL_VERSION_PATCH) "." \
        LPL_TOSTRING(KERNEL_VERSION_TWEAK)

#endif /* !KERNEL_VERSION_H_ */


/** Compile-time configuration parameters, one KEY=value per line. */
#define KERNEL_CONFIG_STRING \
        "KERNEL_VERSION=" KERNEL_VERSION_STRING "\n" \
        "KERNEL_SYSTEM=" KERNEL_SYSTEM_STRING KERNEL_MODE_STRING "\n" \
        "KERNEL_COMPILER=" KERNEL_COMPILER_STRING "\n" \
        "KERNEL_DEBUG=" KERNEL_DEBUG_STRING "\n"

#endif /* !KERNEL_CONFIG_H_ */
// clang-format on
