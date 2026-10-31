// Finder's pictures of BreezeCAD files (Quick Look thumbnail extension, in
// BreezeCAD.app/Contents/PlugIns): the picture a part, assembly or project
// keeps inside its ZIP (preview.png), drawn into the size Finder asks for.
// Files without one get the type's icon from the app, as before.
#import <Cocoa/Cocoa.h>
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

#include <miniz.h>

@interface BZThumbnailProvider : QLThumbnailProvider
@end

@implementation BZThumbnailProvider

- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest*)request
                     completionHandler:(void (^)(QLThumbnailReply* _Nullable, NSError* _Nullable))handler {
    NSData* file = [NSData dataWithContentsOfURL:request.fileURL options:NSDataReadingMappedIfSafe error:nil];
    NSData* png = nil;
    if (file) {
        mz_zip_archive zip{};
        if (mz_zip_reader_init_mem(&zip, file.bytes, file.length, 0)) {
            const int index = mz_zip_reader_locate_file(&zip, "preview.png", nullptr, 0);
            size_t size = 0;
            if (index >= 0)
                if (void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0)) {
                    png = [NSData dataWithBytes:data length:size];
                    mz_free(data);
                }
            mz_zip_reader_end(&zip);
        }
    }
    NSImage* image = png ? [[NSImage alloc] initWithData:png] : nil;
    if (!image || image.size.width <= 0 || image.size.height <= 0) {
        handler(nil, [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError userInfo:nil]);
        return;
    }
    // The picture fitted into the size asked, keeping its proportions (transparent around).
    const CGSize max = request.maximumSize;
    const CGFloat k = MIN(max.width / image.size.width, max.height / image.size.height);
    const CGSize size = CGSizeMake(image.size.width * k, image.size.height * k);
    handler([QLThumbnailReply replyWithContextSize:size
                               currentContextDrawingBlock:^BOOL {
                                   [image drawInRect:NSMakeRect(0, 0, size.width, size.height)];
                                   return YES;
                               }],
            nil);
}

@end
