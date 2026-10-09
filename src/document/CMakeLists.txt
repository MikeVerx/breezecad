add_library(bz_document STATIC
    BzcFile.cpp
    Commands.cpp
    Document.cpp
    EdgeFeature.cpp
    ExtrudeFeature.cpp
    FeatureRegistry.cpp
    FaceReference.cpp
    HoleFeature.cpp
    LoftFeature.cpp
    PatternFeature.cpp
    ShellFeature.cpp
    WorkPlaneFeature.cpp
    JsonIO.cpp
    Recompute.cpp
    RevolveFeature.cpp
    SketchFeature.cpp
    SketchSolidFeature.cpp
    SweepFeature.cpp
    UnknownFeature.cpp)
target_link_libraries(bz_document
    PUBLIC  bz::core bz::sketch bz::json
    PRIVATE bz::miniz)
bz_set_warnings(bz_document)
add_library(bz::document ALIAS bz_document)
