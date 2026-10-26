# PS MTP Explorer
# Original Author: nueex
# Copyright (C) 2026 nueex
# SPDX-License-Identifier: GPL-3.0-or-later
# GNU GPL version 3 or later; see LICENSE and LICENSING.md.
# WITHOUT ANY WARRANTY; see LICENSE.
# Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.

cmake_minimum_required(VERSION 3.16)
project(ps_mtp_explorer VERSION 1.01 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(psmtp src/pkg.c src/transfer_meter.c src/keyboard.c src/metadata.c src/name.c src/device_sound.c src/usb_label.c src/device_watch.c src/locations.c src/ui_format.c src/copy_plan.c src/copy.c src/sha256.c src/mtp.c src/usb_select.c src/navigator.c src/browser.c src/diagnostic.c src/diag_log.c src/usb_module.c)
target_include_directories(psmtp PUBLIC include)
if(DEFINED ENV{OO_PS4_TOOLCHAIN})
  set(_default_sdk "$ENV{OO_PS4_TOOLCHAIN}")
else()
  set(_default_sdk "${CMAKE_CURRENT_SOURCE_DIR}/.tools/sdk/OpenOrbis/PS4Toolchain")
endif()
set(OO_PS4_TOOLCHAIN "${_default_sdk}" CACHE PATH "OpenOrbis PS4 Toolchain directory")
find_path(PSMTP_STB_INCLUDE_DIR stb_image.h
  PATHS "${OO_PS4_TOOLCHAIN}/include/stb" NO_DEFAULT_PATH)
if(NOT PSMTP_STB_INCLUDE_DIR)
  message(FATAL_ERROR "Missing stb headers. Run python tools/bootstrap_openorbis.py or set OO_PS4_TOOLCHAIN to an installed SDK.")
endif()
target_include_directories(psmtp SYSTEM PRIVATE "${PSMTP_STB_INCLUDE_DIR}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(psmtp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
