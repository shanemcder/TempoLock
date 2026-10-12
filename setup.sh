#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v cmake >/dev/null 2>&1; then
  echo 'CMake not found. Install it first (brew install cmake).'
  exit 1
fi
if ! command -v git >/dev/null 2>&1; then
  echo 'Git not found. Install Xcode Command Line Tools.'
  exit 1
fi
if [ ! -f JUCE/CMakeLists.txt ]; then
  if [ -e JUCE ]; then
    echo 'A JUCE folder exists but is incomplete. Move it aside before running setup.'
    exit 1
  fi
  echo 'Downloading JUCE...'
  git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git JUCE
fi
cmake -S . -B build -G Xcode
cmake --build build --config Debug --parallel 4
printf '\nBuild complete. Open the app with:\nopen build/TempoLock_artefacts/Debug/TempoLock.app\n'
