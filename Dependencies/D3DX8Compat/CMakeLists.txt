# D3DX8 replacement for x64 builds: d3dx8.lib from the DirectX 8 SDK exists only for 32-bit.
set(D3DX8COMPAT_SRC
    d3dx8_compat.cpp
    d3dx8math.cpp
)

add_library(deps_d3dx8compat STATIC)

target_sources(deps_d3dx8compat PRIVATE ${D3DX8COMPAT_SRC})

# Only the DX8 headers; linking d3d8lib here would make a cycle (d3d8lib links this library on x64)
target_include_directories(deps_d3dx8compat PRIVATE
    ${dx8_SOURCE_DIR}
)

target_link_libraries(deps_d3dx8compat PRIVATE
    depsi_always # Gives force included headers. Link early.
)
