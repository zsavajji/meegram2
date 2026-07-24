cmake_minimum_required(VERSION 3.20)
project(MeeGram VERSION 0.1.7 LANGUAGES CXX)

# Without this an unset CMAKE_BUILD_TYPE means no -O flag at all, i.e. -O0.
# This codebase leans on std::ranges views and templates, which -O0 punishes hard.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# -O2 rather than -O3: the Cortex-A8 has a small i-cache and -O3's extra inlining
# and unrolling usually costs more in misses than it wins in instruction count.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})

find_package(Td REQUIRED)
find_package(rlottie REQUIRED)
find_package(Qt4 4.7.4 COMPONENTS QtCore QtDBus QtGui QtOpenGL QtSvg QtXml QtDeclarative REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

find_package(Sanitizers QUIET)

option(BUILD_HARMATTAN "Build for MeeGo 1.2 Harmattan Device" OFF)

if (BUILD_HARMATTAN)
    pkg_check_modules(boostable QUIET qdeclarative-boostable)
endif()

# Path to the Qt SDK install (the directory containing Madde/ and Simulator/).
# Settable with -DQT_SDK_PATH=... or the QT_SDK_PATH environment variable.
if(NOT DEFINED QT_SDK_PATH AND DEFINED ENV{QT_SDK_PATH})
    set(QT_SDK_PATH "$ENV{QT_SDK_PATH}")
endif()

# Consumed only by Qt Creator's QML code model - it has no effect on the build.
# Derived from QT_SDK_PATH rather than hardcoding one developer's home directory,
# which is what it used to do.
set(QML_IMPORT_PATH "${QT_SDK_PATH}/Simulator/Qt/gcc/imports" CACHE STRING "QML import path for the Qt Creator code model")

include(cmake/qr_code_generator.cmake)

set(SRC_FILES
    src/AppManager.cpp
    src/Authorization.cpp
    src/BasicGroup.cpp
    src/Chat.cpp
    src/ChatFolderModel.cpp
    src/ChatManager.cpp
    src/ChatModel.cpp
    src/ChatPhotoProvider.cpp
    src/ChatPosition.cpp
    src/Client.cpp
    src/CountryModel.cpp
    src/Emoji.cpp
    src/File.cpp
    src/LanguagePackInfoModel.cpp
    src/Localization.cpp
    src/LottieAnimation.cpp
    src/main.cpp
    src/Message.cpp
    src/MessageContent.cpp
    src/MessageModel.cpp
    src/MessageService.cpp
    src/PluralRules.cpp
    src/QrCodeItem.cpp
    src/Settings.cpp
    src/StorageManager.cpp
    src/SupergroupFullInfo.cpp
    src/Supergroup.cpp
    src/User.cpp
    src/Utils.cpp
)

set(HEADER_FILES
    src/AppManager.hpp
    src/Authorization.hpp
    src/BasicGroup.hpp
    src/Chat.hpp
    src/ChatFolderModel.hpp
    src/ChatManager.hpp
    src/ChatModel.hpp
    src/ChatPhotoProvider.hpp
    src/ChatPosition.hpp
    src/Client.hpp
    src/Common.hpp
    src/CountryModel.hpp
    src/Emoji.hpp
    src/File.hpp
    src/LanguagePackInfoModel.hpp
    src/Localization.hpp
    src/LottieAnimation.hpp
    src/Message.hpp
    src/MessageContent.hpp
    src/MessageModel.hpp
    src/MessageService.hpp
    src/PluralRules.hpp
    src/QrCodeItem.hpp
    src/ScopeTimer.hpp
    src/Settings.hpp
    src/StorageManager.hpp
    src/Supergroup.hpp
    src/SupergroupFullInfo.hpp
    src/User.hpp
    src/Utils.hpp
)

set(QRC_FILES
    resources/emoji_1.qrc
    resources/emoji_2.qrc
    resources/emoji_3.qrc
    resources/emoji_4.qrc
    resources/emoji_5.qrc
    resources/emoji_6.qrc
    resources/fonts.qrc
    resources/images.qrc
    resources/qml.qrc
    resources/tgs.qrc
)

add_executable(meegram ${SRC_FILES} ${HEADER_FILES} ${QRC_FILES})

set_target_properties(meegram PROPERTIES AUTOMOC ON AUTORCC ON)

target_compile_options(meegram PRIVATE -Wall -Wextra -pedantic $<IF:$<CONFIG:Debug>, -Werror, -Wno-psabi>)

# Let the linker drop unreferenced code/data. Matters here because TDLib is linked
# statically and pulls in a large object set.
target_compile_options(meegram PRIVATE $<$<NOT:$<CONFIG:Debug>>:-ffunction-sections -fdata-sections>)
target_link_options(meegram PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Wl,--gc-sections>)

# qDebug()/qWarning() go to syslog on Harmattan; there are live call sites on the
# translation and language-pack paths. Compile them out of non-debug builds.
target_compile_definitions(meegram PRIVATE $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>)

# Opt-in profiling: enables the ScopeTimer instrumentation in src/ScopeTimer.hpp.
option(MEEGRAM_PROFILE "Enable on-device scope timing instrumentation" OFF)
target_compile_definitions(meegram PRIVATE $<$<BOOL:${MEEGRAM_PROFILE}>:MEEGRAM_PROFILE>)

# Back QDeclarativeView with a QGLWidget viewport. On by default, but exposed as a
# switch because it is the one change here whose effect on Harmattan's compositor
# is worth A/B testing on the real device.
option(MEEGRAM_GL_VIEWPORT "Use a QGLWidget viewport for QDeclarativeView" ON)
target_compile_definitions(meegram PRIVATE $<$<BOOL:${MEEGRAM_GL_VIEWPORT}>:MEEGRAM_GL_VIEWPORT>)

target_link_libraries(meegram PRIVATE
    Lib::QRCode
    Td::TdStatic
    rlottie::rlottie
    Qt4::QtCore
    Qt4::QtDBus
    Qt4::QtDeclarative
    Qt4::QtGui
    Qt4::QtOpenGL
    Qt4::QtSvg
    Qt4::QtXml
    ZLIB::ZLIB
    Threads::Threads
)

# ASan costs 2-3x runtime and a large RSS increase - never let it reach the device.
# The COMMAND check also keeps configure working when the sanitizers-cmake submodule
# has not been initialised, which is the state a fresh clone starts in.
if (NOT BUILD_HARMATTAN AND COMMAND add_sanitizers)
    add_sanitizers(meegram)
endif()

if (BUILD_HARMATTAN)
    # Target ABI for the N9's OMAP3630 Cortex-A8. -mthumb shrinks code, which helps
    # the small i-cache.
    #
    # hard, not softfp: the Harmattan sysroot's libc reports
    #   Tag_ABI_VFP_args: VFP registers
    # i.e. floating-point arguments are passed in VFP registers. Building softfp
    # against it fails at link with "uses VFP register arguments, output does not".
    # Re-check with:
    #   readelf -A "$SYSROOT/lib/libc.so.6" | grep Tag_ABI_VFP_args
    target_compile_options(meegram PRIVATE
        -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard -mthumb)

    target_compile_options(meegram PRIVATE ${boostable_CFLAGS})
    target_include_directories(meegram PRIVATE ${boostable_INCLUDE_DIRS})

    target_link_options(meegram PRIVATE -Wl,-rpath,/opt/meegram/lib -Wl,--dynamic-linker=/lib/ld-linux.so.3 -pie -rdynamic)

    target_link_libraries(meegram PRIVATE ${boostable_LIBRARIES} $<$<BOOL:${SANITIZE_ADDRESS}>:-static-libasan> -static-libstdc++)

    if(NOT EXISTS "${CMAKE_BINARY_DIR}/debian")
        file(COPY ${CMAKE_SOURCE_DIR}/debian DESTINATION ${CMAKE_BINARY_DIR})
    endif()

    # CMake rewrites RPATH on install and, by default, drops everything - the
    # -Wl,-rpath above only applies to the binary in the build tree. Without this the
    # installed binary has no RPATH at all and cannot find the libraries below.
    # BUILD_WITH_INSTALL_RPATH stays off so the build-tree binary keeps the paths
    # that let it link.
    set_target_properties(meegram PROPERTIES
        INSTALL_RPATH "/opt/meegram/lib"
        INSTALL_RPATH_USE_LINK_PATH FALSE
        BUILD_WITH_INSTALL_RPATH FALSE)

    install(FILES resources/meegram_harmattan.desktop DESTINATION /usr/share/applications)
    install(FILES resources/meegram80.png DESTINATION /usr/share/icons/hicolor/80x80/apps)
    install(FILES resources/meegram-splash.png DESTINATION /opt/meegram/share)

    install(TARGETS meegram DESTINATION /opt/meegram/bin)

    # TDLib is linked statically, but it pulls in OpenSSL *shared*, so the binary
    # needs libssl.so.3 / libcrypto.so.3 at runtime. Harmattan ships OpenSSL 0.9.8,
    # so ours has to travel inside the package or the app dies at startup with
    # "libcrypto.so.3: cannot open shared object file".
    #
    # tools/setup-dependencies.sh leaves them in build/crypto/lib rather than
    # installing them into the sysroot; point this elsewhere if that changes.
    set(MEEGRAM_OPENSSL_LIB_DIR "${CMAKE_SOURCE_DIR}/build/crypto/lib"
        CACHE PATH "Directory holding the cross-built libssl/libcrypto to ship")

    file(GLOB MEEGRAM_OPENSSL_LIBS
        "${MEEGRAM_OPENSSL_LIB_DIR}/libssl.so*"
        "${MEEGRAM_OPENSSL_LIB_DIR}/libcrypto.so*")

    if(MEEGRAM_OPENSSL_LIBS)
        install(FILES ${MEEGRAM_OPENSSL_LIBS} DESTINATION /opt/meegram/lib)
    else()
        message(WARNING
            "No libssl/libcrypto found in ${MEEGRAM_OPENSSL_LIB_DIR}. The package will "
            "build but the app will fail on the device with 'libcrypto.so.3: cannot open "
            "shared object file'. Set -DMEEGRAM_OPENSSL_LIB_DIR to where they live.")
    endif()

    add_custom_target(package
        COMMAND mad dpkg-buildpackage -nc -uc -us
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Debian package with mad..."
    )
endif()
