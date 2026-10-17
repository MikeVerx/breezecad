add_library(bz_document STATIC
    Appearance.cpp
    BzcFile.cpp
    Commands.cpp
    Document.cpp
    DocumentTypes.cpp
    CoilFeature.cpp
    SectionFeature.cpp
    ShellFeature.cpp
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
    StandardPacks.cpp
    StandardParts.cpp
    SweepFeature.cpp
    ThreadFeature.cpp
    UnknownFeature.cpp
    WorkAxisFeature.cpp
    WorkPointFeature.cpp)
target_link_libraries(bz_document
    PUBLIC  bz::core bz::sketch bz::json
    PRIVATE bz::miniz)
bz_set_warnings(bz_document)
add_library(bz::document ALIAS bz_document)
