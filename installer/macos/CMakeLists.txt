# macOS: the Quick Look thumbnail extension (Finder's pictures of parts,
# assemblies and projects). package-macos.sh puts the .appex into
# BreezeCAD.app/Contents/PlugIns and signs it with Thumbnails.entitlements.
add_executable(bz_mac_thumbnails ThumbnailProvider.mm)
set_source_files_properties(ThumbnailProvider.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-fapplication-extension")
target_link_libraries(bz_mac_thumbnails PRIVATE bz::miniz "-framework Cocoa" "-framework QuickLookThumbnailing")
# An app extension's entry is NSExtensionMain (from Foundation).
target_link_options(bz_mac_thumbnails PRIVATE -e _NSExtensionMain -fapplication-extension)
set_target_properties(bz_mac_thumbnails PROPERTIES OUTPUT_NAME BreezeCADThumbnails)
