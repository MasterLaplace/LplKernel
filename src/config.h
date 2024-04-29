/**************************************************************************
 * LplKernel v0.0.0
 *
 * LplKernel is a C kernel iso for Laplace. It is a simple kernel that
 * provides a basic set of features to run a C program.
 *
 * This file is part of the LplKernel project that is under Anti-NN License.
 * https://github.com/MasterLaplace/Anti-NN_LICENSE
 * Copyright © 2024 by @MasterLaplace, All rights reserved.
 *
 * LplKernel is a free software: you can redistribute it and/or modify
 * it under the terms of the Anti-NN License as published by the
 * Open Source Initiative. See the Anti-NN License for more details.
 *
 * @file config.h
 * @brief Compile-Time Configuration Parameters for LplKernel.
 *
 * @author @MasterLaplace
 * @version 0.0.0
 * @date 2024-03-28
 **************************************************************************/


#ifndef CONFIG_H_
    #define CONFIG_H_

#ifdef __cplusplus
    #include <utility>
    #include <type_traits>
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DISTRIBUTION_H_
    #define DISTRIBUTION_H_

////////////////////////////////////////////////////////////
// Identify the Compiler
////////////////////////////////////////////////////////////
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
    #error [Config@Distribution]: This compiler is not supported by KERNEL library.
#endif


////////////////////////////////////////////////////////////
// Identify the Operating System
////////////////////////////////////////////////////////////
#if defined(_WIN32) || defined(__WIN32__) || defined(KERNEL_COMPILER_MINGW) || defined(KERNEL_COMPILER_CYGWIN)

    #define KERNEL_SYSTEM_WINDOWS
    #define KERNEL_SYSTEM_STRING "Windows"

// Android is based on the Linux kernel, so it has to appear before Linux
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

#elif defined(LAPLACE_KERNEL_PANIC)

    #define KERNEL_SYSTEM_KERNEL
    #define KERNEL_SYSTEM_STRING "Laplace Kernel"

#else
    #error [Config@Distribution]: This operating system is not supported by KERNEL library.
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

    ////////////////////////////////////////////////////////////
    // Define a macro to handle cpp features compatibility
    ////////////////////////////////////////////////////////////
    /** Usage:
     * @example
     * void func() KERNEL_CPP14([[deprecated]]);
     *
     * @example
     * void func() KERNEL_CPP([[deprecated]], 14);
    \**********************************************************/
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

////////////////////////////////////////////////////////////
// Define helpers to create portable import / export macros for each module
////////////////////////////////////////////////////////////
#if defined(KERNEL_SYSTEM_WINDOWS)

    // Windows compilers need specific (and different) keywords for export and import
    #define KERNEL_API_EXPORT extern "C" __declspec(dllexport)
    #define KERNEL_API_IMPORT KERNEL_EXTERN_C __declspec(dllimport)

    // For Visual C++ compilers, we also need to turn off this annoying C4251 warning
    #ifdef _MSC_VER

        #pragma warning(disable : 4251)

    #endif

#else // Linux, FreeBSD, Mac OS X

    #if __GNUC__ >= 4

        // GCC 4 has special keywords for showing/hidding symbols,
        // the same keyword is used for both importing and exporting
        #define KERNEL_API_EXPORT extern "C" __attribute__ ((__visibility__ ("default")))
        #define KERNEL_API_IMPORT KERNEL_EXTERN_C __attribute__ ((__visibility__ ("default")))

    #else

        // GCC < 4 has no mechanism to explicitely hide symbols, everything's exported
        #define KERNEL_API_EXPORT extern "C"
        #define KERNEL_API_IMPORT KERNEL_EXTERN_C

    #endif

#endif


#ifdef KERNEL_SYSTEM_WINDOWS

    // Windows compilers use a different name for the main function
    #define KERNEL_GUI_MAIN(hInstance, hPrevInstance, lpCmdLine, nCmdShow) WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
    #define KERNEL_MAIN(ac, av, env) main(int ac, char *av[], char *env[])

#elif defined(KERNEL_SYSTEM_ANDROID)

    // Android doesn't need a main function
    #define KERNEL_GUI_MAIN(app) android_main(struct android_app* app)
    #define KERNEL_MAIN

#elif defined(KERNEL_SYSTEM_MACOS)

    // On MacOS X, we use a Unix main function
    #define KERNEL_MAIN(ac, av, env, apple) main(int ac, char *av[], char *env[], char *apple[])

#else

    // Other platforms should use the standard main function
    #define KERNEL_MAIN(ac, av, env) main(int ac, char *av[], char *env[])
#endif

////////////////////////////////////////////////////////////
// Define portable NULL pointer using C++11 nullptr keyword
////////////////////////////////////////////////////////////
#if defined(__cplusplus) && __cplusplus >= 201103L
#elif !defined(NULL)
    #define nullptr ((void*)0)
#else
    #define nullptr NULL
#endif

////////////////////////////////////////////////////////////
// Define a portable debug macro
////////////////////////////////////////////////////////////
#if (defined(_DEBUG) || defined(DEBUG)) && !defined(NDEBUG)

    #define KERNEL_DEBUG
    #define KERNEL_DEBUG_STRING "Debug"

#else
    #define KERNEL_DEBUG_STRING "Release"
#endif


#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define __GNUC_PREREQ(maj, min) 0
#endif

////////////////////////////////////////////////////////////
// Define a portable way to declare a function as deprecated
////////////////////////////////////////////////////////////
/** Usage:
 * @example "for functions"
 *   KERNEL_DEPRECATED void func();
 * @example "for structs"
 *   struct KERNEL_DEPRECATED MyStruct { ... };
 * @example "for enums"
 *   enum KERNEL_DEPRECATED MyEnum { ... };
 *   enum MyEnum {
 *        MyEnum1 = 0,
 *        MyEnum2 KERNEL_DEPRECATED,
 *        MyEnum3
 *   };
 * @example "for classes"
 *   class KERNEL_DEPRECATED MyClass { ... };
\**********************************************************/
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


////////////////////////////////////////////////////////////
// Define a portable way for packing structures
////////////////////////////////////////////////////////////
/** Usage:
 * @example
 * PACKED(struct MyStruct
 * {
 *     int a;
 *     char b;
 *     ...
 * });
\**********************************************************/
#if defined(__GNUC__) || defined(__GNUG__)
    #define PACKED( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif _MSC_VER
    #define PACKED( __Declaration__ ) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))
#else
    #define PACKED( __Declaration__ ) __Declaration__
#endif

#endif /* !DISTRIBUTION_H_ */


#ifndef VERSION_H_
    #define VERSION_H_

////////////////////////////////////////////////////////////
// Define the KERNEL version
////////////////////////////////////////////////////////////
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
    #define KERNEL_VERSION_TWEAK 0
#endif

////////////////////////////////////////////////////////////
// Define the KERNEL version number
////////////////////////////////////////////////////////////
#define KERNEL_VERSION_NUM \
        (KERNEL_VERSION_MAJOR * 1000000 + \
        KERNEL_VERSION_MINOR * 10000 + \
        KERNEL_VERSION_PATCH * 100 + \
        KERNEL_VERSION_TWEAK)

#define KERNEL_PREREQ_VERSION(maj, min, pat) (KERNEL_VERSION_NUM >= (maj * 1000000 + min * 10000 + pat * 100))

////////////////////////////////////////////////////////////
// Define the KERNEL version concatenated
////////////////////////////////////////////////////////////
#define KERNEL_VERSION_CCT KERNEL_VERSION_MAJOR##_##KERNEL_VERSION_MINOR##_##KERNEL_VERSION_PATCH##_##KERNEL_VERSION_TWEAK


////////////////////////////////////////////////////////////
// Define the KERNEL version string
////////////////////////////////////////////////////////////
// Helper macro to convert a macro to a string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define KERNEL_VERSION_STRING \
        TOSTRING(KERNEL_VERSION_MAJOR) "." \
        TOSTRING(KERNEL_VERSION_MINOR) "." \
        TOSTRING(KERNEL_VERSION_PATCH) "." \
        TOSTRING(KERNEL_VERSION_TWEAK)

#endif /* !VERSION_H_ */


////////////////////////////////////////////////////////////
// Compile-Time Configuration Parameters
////////////////////////////////////////////////////////////
#define KERNEL_CONFIG_STRING \
        "KERNEL_VERSION=" KERNEL_VERSION_STRING "\n" \
        "KERNEL_SYSTEM=" KERNEL_SYSTEM_STRING "\n" \
        "KERNEL_COMPILER=" KERNEL_COMPILER_STRING "\n" \
        "KERNEL_DEBUG=" KERNEL_DEBUG_STRING "\n"

#endif /* !CONFIG_H_ */
