#!/usr/bin/env bash
set -euo pipefail
camera_source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
camera_build_dir="${HOME}/.cache/usb_camera_viewer-build"
cmake -S "${camera_source_dir}" -B "${camera_build_dir}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "${camera_build_dir}" --parallel 2
exec "${camera_build_dir}/usb_camera_viewer" "$@"
