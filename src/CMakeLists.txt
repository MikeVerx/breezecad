# Layers, from the bottom up. Each layer only depends on layers above it
# in this list (see docs/ARCHITECTURE.md):
#
#   core      — ids, units, expressions, logging           (no dependencies)
#   sketch    — 2D sketch model and constraint solver       (core)
#   document  — features, parameters, undo, .bzc format     (core, sketch)
#   kernel    — builds solids from a document with OCCT     (document)
#   render    — 3D viewport on top of OCCT visualization    (kernel)
#   ui        — Dear ImGui interface, knows nothing of OCCT (document)
#   app       — the executable: wires everything together   (all)

add_subdirectory(core)
add_subdirectory(sketch)
add_subdirectory(document)
# kernel: added next

if(BZ_BUILD_APP)
    add_subdirectory(render)
    add_subdirectory(ui)
    add_subdirectory(app)
endif()
