generate_headers:
    python3 tools/spv-binary-container-generator.py external/SPIRV-Headers/include/spirv/1.0/spirv.core.grammar.json common/include/spirv_binary_container.hpp

setup: generate_headers
    cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug

build:
    ninja -C build
