add_library(bz_document STATIC
    Appearance.cpp
    BzcFile.cpp
    Commands.cpp
    Document.cpp
    CoilFeature.cpp
    SectionFeature.cpp
    EdgeFeature.cpp
    ExtrudeFeature.cpp
    FeatureRegistry.cpp
    FaceReference.cpp
    HoleFeature.cpp
    LoftFeature.cpp
    PatternFeature.cpp
    WorkPlaneFeature.cpp
    JsonIO.cpp
    Recompute.cpp
    RevolveFeature.cpp
    SketchFeature.cpp
    SketchSolidFeature.cpp
    SweepFeature.cpp
    ThreadFeature.cpp
    UnknownFeature.cpp
    WorkAxisFeature.cpp)
target_link_libraries(bz_document
    PUBLIC  bz::core bz::sketch bz::json
    PRIVATE bz::miniz)
bz_set_warnings(bz_document)
add_library(bz::document ALIAS bz_document)
