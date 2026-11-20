#include "EditorFileDialog.hpp"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace VulkCore {

std::filesystem::path pickEditorFile(
    const char* title, bool scene, const std::filesystem::path& directory
) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = [NSString stringWithUTF8String:title];
        panel.allowsMultipleSelection = NO;
        panel.canChooseDirectories = NO;
        if (!directory.empty()) {
            panel.directoryURL = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:directory.c_str()] isDirectory:YES];
        }
        NSArray<NSString*>* extensions = scene ? @[@"scene"] :
            @[@"png", @"jpg", @"jpeg", @"tga", @"bmp", @"psd", @"hdr"];
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* extension in extensions) {
            if (UTType* type = [UTType typeWithFilenameExtension:extension]) {
                [types addObject:type];
            }
        }
        panel.allowedContentTypes = types;
        if ([panel runModal] == NSModalResponseOK) {
            return panel.URL.fileSystemRepresentation;
        }
        return {};
    }
}

} // namespace VulkCore
