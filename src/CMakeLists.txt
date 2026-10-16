# Layers, from the bottom up. Each layer only depends on layers above it
# in this list (see docs/ARCHITECTURE.md):
#
#   core      — ids, units, expressions, logging           (no dependencies)
#   sketch    — 2D sketch model and constraint solver       (core)
#   document  — features, parameters, undo, .bzc format     (core, sketch)
#   assembly  — components, constraints and their solver    (document)
#   kernel    — builds solids from a document with OCCT     (document, assembly)
#   modules   — module manifest, loading, C ABI for modules (kernel)
#   render    — 3D viewport on top of OCCT visualization    (kernel)
#   ui        — Dear ImGui interface, knows nothing of OCCT (document)
#   app       — the executable: wires everything together   (all)

add_subdirectory(core)
add_subdirectory(sketch)
add_subdirectory(document)
add_subdirectory(assembly)
add_subdirectory(kernel)
add_subdirectory(modules)

if(BZ_BUILD_ANDROID_APP)   # Android: no GLFW/ImGui yet — 3D view and touch (docs/dev/MOBILE-PORT.md)
    add_subdirectory(render)
    add_subdirectory(android)
endif()

if(BZ_BUILD_APP)
    add_subdirectory(platform)
    add_subdirectory(render)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
