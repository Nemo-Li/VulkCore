#include "EffectEditorView.hpp"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

NSString* mediaType(NSString* extension) {
    if ([extension isEqualToString:@"html"]) return @"text/html";
    if ([extension isEqualToString:@"js"]) return @"text/javascript";
    if ([extension isEqualToString:@"css"]) return @"text/css";
    if ([extension isEqualToString:@"json"] || [extension isEqualToString:@"map"]) {
        return @"application/json";
    }
    return @"text/plain";
}

bool isStudioPage(NSURL* url) {
    return [url.scheme isEqualToString:@"vulkcore-vfx"] && [url.host isEqualToString:@"studio"];
}

} // namespace

@interface VulkCoreEffectHost : NSObject <WKURLSchemeHandler, WKNavigationDelegate, WKUIDelegate, WKDownloadDelegate>
@property(nonatomic, strong) NSURL* root;
@end

@implementation VulkCoreEffectHost

- (void)webView:(WKWebView*)view startURLSchemeTask:(id<WKURLSchemeTask>)task {
    NSURL* url = task.request.URL;
    if (isStudioPage(url) && [task.request.HTTPMethod isEqualToString:@"GET"]) {
        NSString* path = url.path.length > 1 ? [url.path substringFromIndex:1] : @"index.html";
        const fs::path root(self.root.fileSystemRepresentation);
        std::error_code error;
        const fs::path file = fs::canonical(root / path.fileSystemRepresentation, error);
        if (!error && fs::is_regular_file(file, error)) {
            const auto relative = file.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..") {
                const auto type = mediaType([path pathExtension]);
                NSURL* fileUrl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
                if (NSData* data = [NSData dataWithContentsOfURL:fileUrl]) {
                    NSDictionary* headers = @{
                        @"Content-Type": [type stringByAppendingString:@"; charset=utf-8"],
                        @"Content-Security-Policy":
                            @"default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; "
                             "img-src 'self' data: blob:; connect-src 'self' data: blob:; object-src 'none'",
                        @"Cross-Origin-Resource-Policy": @"same-origin"
                    };
                    NSHTTPURLResponse* response = [[NSHTTPURLResponse alloc] initWithURL:url
                        statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:headers];
                    [task didReceiveResponse:response];
                    [task didReceiveData:data];
                    [task didFinish];
                    return;
                }
            }
        }
    }
    [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain
        code:NSURLErrorFileDoesNotExist userInfo:nil]];
}

- (void)webView:(WKWebView*)view stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    // Responses complete synchronously before WebKit can cancel a task.
}

- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action
        decisionHandler:(void (^)(WKNavigationActionPolicy))reply {
    NSURL* url = action.request.URL;
    if (@available(macOS 11.3, *)) {
        if (action.shouldPerformDownload && [url.scheme isEqualToString:@"blob"] &&
            action.sourceFrame.isMainFrame) {
            reply(WKNavigationActionPolicyDownload);
            return;
        }
    }
    reply(isStudioPage(url) ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}

- (void)webView:(WKWebView*)view navigationAction:(WKNavigationAction*)action
        didBecomeDownload:(WKDownload*)download API_AVAILABLE(macos(11.3)) {
    download.delegate = self;
}

- (void)download:(WKDownload*)download decideDestinationUsingResponse:(NSURLResponse*)response
        suggestedFilename:(NSString*)name completionHandler:(void (^)(NSURL*))reply API_AVAILABLE(macos(11.3)) {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = name.lastPathComponent;
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
        reply(result == NSModalResponseOK ? panel.URL : nil);
    }];
}

- (void)webView:(WKWebView*)view runOpenPanelWithParameters:(WKOpenPanelParameters*)parameters
        initiatedByFrame:(WKFrameInfo*)frame completionHandler:(void (^)(NSArray<NSURL*>*))reply {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowsMultipleSelection = parameters.allowsMultipleSelection;
    panel.canChooseDirectories = parameters.allowsDirectories;
    panel.canChooseFiles = YES;
    [panel beginSheetModalForWindow:view.window completionHandler:^(NSModalResponse result) {
        reply(result == NSModalResponseOK ? panel.URLs : nil);
    }];
}

@end

namespace VulkCore {

struct EffectEditorView::Impl {
    NSWindow* window;
    VulkCoreEffectHost* host;
    WKWebView* view;
};

EffectEditorView::EffectEditorView(Window* window)
    : impl_(std::make_unique<Impl>()) {
    impl_->window = glfwGetCocoaWindow(window);
}

EffectEditorView::~EffectEditorView() {
    [impl_->view removeFromSuperview];
}

bool EffectEditorView::show(float x, float y, float width, float height) {
    NSView* container = impl_->window.contentView;
    if (!container || width <= 0 || height <= 0) return false;
    if (!impl_->view) {
        std::error_code error;
        const auto root = fs::canonical(VULKCORE_EFFECT_EDITOR_DIR, error);
        if (error || !fs::is_regular_file(root / "index.html", error)) return false;

        impl_->host = [[VulkCoreEffectHost alloc] init];
        impl_->host.root = [NSURL fileURLWithPath:[NSString stringWithUTF8String:root.c_str()]
            isDirectory:YES];
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        [config setURLSchemeHandler:impl_->host forURLScheme:@"vulkcore-vfx"];
        impl_->view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:config];
        impl_->view.navigationDelegate = impl_->host;
        impl_->view.UIDelegate = impl_->host;
        [container addSubview:impl_->view positioned:NSWindowAbove relativeTo:nil];
        [impl_->view loadRequest:[NSURLRequest requestWithURL:
            [NSURL URLWithString:@"vulkcore-vfx://studio/index.html"]]];
    }
    const float bottom = container.isFlipped ? y : CGRectGetHeight(container.bounds) - y - height;
    const NSRect frame = NSMakeRect(x, bottom, width, height);
    if (!NSEqualRects(impl_->view.frame, frame)) impl_->view.frame = frame;
    if (impl_->view.hidden) {
        impl_->view.hidden = NO;
        [impl_->view evaluateJavaScript:@"delete document.documentElement.dataset.hostHidden"
            completionHandler:nil];
    }
    return true;
}

void EffectEditorView::hide() {
    if (!impl_->view || impl_->view.hidden) return;
    if ([impl_->window.firstResponder isKindOfClass:[NSView class]] &&
        [(NSView*)impl_->window.firstResponder isDescendantOf:impl_->view]) {
        [impl_->window makeFirstResponder:impl_->window.contentView];
    }
    impl_->view.hidden = YES;
    [impl_->view evaluateJavaScript:@"document.documentElement.dataset.hostHidden = 'true'"
        completionHandler:nil];
}

} // namespace VulkCore
