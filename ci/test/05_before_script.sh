#!/usr/bin/env bash
#
# Copyright (c) 2018-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

# Make sure default datadir does not exist and is never read by creating a dummy file
if [ "$CI_OS_NAME" == "macos" ]; then
  echo > "${HOME}/Library/Application Support/Bitcoin"
else
  DOCKER_EXEC echo \> \$HOME/.bitcoin
fi

DOCKER_EXEC mkdir -p "${DEPENDS_DIR}/SDKs" "${DEPENDS_DIR}/sdk-sources"

OSX_SDK_BASENAME="Xcode-${XCODE_VERSION}-${XCODE_BUILD_ID}-extracted-SDK-with-libcxx-headers"

if [ -n "$XCODE_VERSION" ] && [ ! -d "${DEPENDS_DIR}/SDKs/${OSX_SDK_BASENAME}" ]; then
  OSX_SDK_FILENAME="${OSX_SDK_BASENAME}.tar.gz"
  OSX_SDK_PATH="${DEPENDS_DIR}/sdk-sources/${OSX_SDK_FILENAME}"
  if [ ! -f "$OSX_SDK_PATH" ]; then
    DOCKER_EXEC curl --location --fail "${SDK_URL}/${OSX_SDK_FILENAME}" -o "$OSX_SDK_PATH"
  fi
  DOCKER_EXEC tar -C "${DEPENDS_DIR}/SDKs" -xf "$OSX_SDK_PATH"
fi

if [ -n "$ANDROID_HOME" ] && [ ! -d "$ANDROID_HOME" ]; then
  ANDROID_TOOLS_PATH=${DEPENDS_DIR}/sdk-sources/android-tools.zip
  if [ ! -f "$ANDROID_TOOLS_PATH" ]; then
    DOCKER_EXEC curl --location --fail "${ANDROID_TOOLS_URL}" -o "$ANDROID_TOOLS_PATH"
  fi
  DOCKER_EXEC mkdir -p "${ANDROID_HOME}/cmdline-tools"
  DOCKER_EXEC unzip -o "$ANDROID_TOOLS_PATH" -d "${ANDROID_HOME}/cmdline-tools"
  DOCKER_EXEC "yes | ${ANDROID_HOME}/cmdline-tools/tools/bin/sdkmanager --install \"build-tools;${ANDROID_BUILD_TOOLS_VERSION}\" \"platform-tools\" \"platforms;android-${ANDROID_API_LEVEL}\" \"ndk;${ANDROID_NDK_VERSION}\""
fi

if [[ ${USE_MEMORY_SANITIZER} == "true" ]]; then
  # Use BDB compiled using install_db4.sh script to work around linking issue when using BDB
  # from depends. See https://github.