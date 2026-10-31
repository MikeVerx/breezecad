// macOS: Finder's "open these documents" reaches the application delegate.
// GLFW installs its own delegate and has no callback for it, so the method
// is added to that delegate's class at run time.
#include "platform/OpenFiles.h"

#import <Cocoa/Cocoa.h>
#include <objc/runtime.h>

#include <mutex>

namespace bz::platform {
namespace {

std::mutex                         gMutex;
std::vector<std::filesystem::path> gAsked;

void Ask(NSArray<NSURL*>* urls) {
    std::lock_guard lock(gMutex);
    for (NSURL* url in urls)
        if (url.isFileURL && url.path) gAsked.emplace_back(std::u8string(reinterpret_cast<const char8_t*>(url.path.UTF8String)));
}

} // namespace

void ListenForOpenRequests() {
    id delegate = NSApp.delegate;
    if (!delegate) return;
    Class cls = object_getClass(delegate);
    // application:openURLs: (10.13+) is what Finder sends; openFiles: for older senders.
    IMP urls = imp_implementationWithBlock(^(id, NSApplication*, NSArray<NSURL*>* list) { Ask(list); });
    class_addMethod(cls, @selector(application:openURLs:), urls, "v@:@@");
    IMP files = imp_implementationWithBlock(^(id, NSApplication* app, NSArray<NSString*>* paths) {
        NSMutableArray<NSURL*>* list = [NSMutableArray array];
        for (NSString* p in paths) [list addObject:[NSURL fileURLWithPath:p]];
        Ask(list);
        [app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
    });
    class_addMethod(cls, @selector(application:openFiles:), files, "v@:@@");
}

std::vector<std::filesystem::path> TakeOpenRequests() {
    std::lock_guard lock(gMutex);
    return std::exchange(gAsked, {});
}

} // namespace bz::platform
