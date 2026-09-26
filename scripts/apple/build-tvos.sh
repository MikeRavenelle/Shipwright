#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build-tvos"
BUNDLE_ID="com.harbourmasters.soh"
DEVELOPMENT_TEAM=""
BUILD_TYPE="Debug"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle-id) BUNDLE_ID="$2"; shift 2 ;;
        --team)      DEVELOPMENT_TEAM="$2"; shift 2 ;;
        --release)   BUILD_TYPE="Release"; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo "==> Configuring tvOS build in ${BUILD_DIR}"
echo "    Bundle ID : ${BUNDLE_ID}"
echo "    Team      : ${DEVELOPMENT_TEAM:-<none – unsigned>}"
echo "    Build type: ${BUILD_TYPE}"

cmake \
    -S "${REPO_ROOT}" \
    -B "${BUILD_DIR}" \
    -G Xcode \
    -DCMAKE_SYSTEM_NAME=tvOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DBUNDLE_ID="${BUNDLE_ID}" \
    -DDEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
    -DSIGN_LIBRARY=OFF \
    -DBUILD_REMOTE_CONTROL=0 \
    -DSDL_SENSOR=OFF \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo ""
echo "==> Configuration complete."
echo "    Open ${BUILD_DIR}/Ship.xcodeproj in Xcode,"
echo "    select the 'soh' scheme, choose your Apple TV device or simulator,"
echo "    and press Run (or Build)."
echo ""
echo "    To sign for a real Apple TV, set a Development Team in Xcode or"
echo "    re-run this script with: --team <YOUR_TEAM_ID>"
