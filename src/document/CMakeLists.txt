add_library(bz_document STATIC
    BzcFile.cpp
    Commands.cpp
    Document.cpp
    EdgeFeature.cpp
    ExtrudeFeature.cpp
    FeatureRegistry.cpp
    HoleFeature.cpp
    JsonIO.cpp
    Recompute.cpp
    RevolveFeature.cpp
    SketchFeature.cpp
    SketchSolidFeature.cpp
    UnknownFeature.cpp)
target_link_libraries(bz_document
    PUBLIC  bz::core bz::sketch bz::json
    PRIVATE bz::miniz)
bz_set_warnings(bz_document)
add_library(bz::document ALIAS bz_document)
