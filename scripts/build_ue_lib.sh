#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
UE_THIRDPARTY="$PROJECT_ROOT/ue/CosmosimPlugin/ThirdParty/libcosmosim"

echo "=== Building libcosmosim ==="
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$PROJECT_ROOT"
cmake --build "$BUILD_DIR" --target cosmosim_shared

echo "=== Copying to UE plugin ==="
mkdir -p "$UE_THIRDPARTY/include" "$UE_THIRDPARTY/lib"

cp "$PROJECT_ROOT/src/cosmosim_api.h" "$UE_THIRDPARTY/include/"
cp "$PROJECT_ROOT/src/body.h" "$UE_THIRDPARTY/include/"

case "$(uname)" in
    Darwin)
        cp "$BUILD_DIR/libcosmosim.dylib" "$UE_THIRDPARTY/lib/"
        echo "Copied libcosmosim.dylib"
        ;;
    Linux)
        cp "$BUILD_DIR/libcosmosim.so" "$UE_THIRDPARTY/lib/"
        echo "Copied libcosmosim.so"
        ;;
    MINGW*|CYGWIN*|MSYS*)
        cp "$BUILD_DIR/cosmosim.dll" "$UE_THIRDPARTY/lib/"
        cp "$BUILD_DIR/cosmosim.lib" "$UE_THIRDPARTY/lib/" 2>/dev/null || true
        echo "Copied cosmosim.dll"
        ;;
esac

echo "=== Running physics tests ==="
"$BUILD_DIR/test_physics"

echo ""
echo "=== Done ==="
echo "Headers: $UE_THIRDPARTY/include/"
echo "Library: $UE_THIRDPARTY/lib/"
echo ""
echo "Next: Open UE project and enable CosmosimPlugin"
