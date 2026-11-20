#include "EditorTrash.hpp"

#include <stdexcept>

#import <Foundation/Foundation.h>

namespace VulkCore {

void trashEditorFile(const std::filesystem::path& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithFileSystemRepresentation:path.c_str()
            isDirectory:NO relativeToURL:nil];
        NSError* error = nil;
        if (![[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nullptr error:&error]) {
            const char* message = error.localizedDescription.UTF8String;
            throw std::runtime_error(message ? message : "The system Trash could not recycle the file.");
        }
    }
}

} // namespace VulkCore
