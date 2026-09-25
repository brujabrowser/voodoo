// Minimal ObjC slice in the shape of
// Source/WebKit/UIProcess/API/Cocoa/WKWebView.h -- used to prove brujac's
// cocoa frontend can ingest real @interface / @property / ObjC methods,
// including completion-handler → Promise. Not a copy of the Apple header.

@class WKWebViewConfiguration;
@class WKNavigation;

#if TARGET_OS_IPHONE
@interface WKWebView : UIView
#else
@interface WKWebView : NSView
#endif

@property (nonatomic, readonly, copy) WKWebViewConfiguration *configuration;
@property (nullable, nonatomic, readonly, copy) NSString *title;
@property (nullable, nonatomic, readonly, copy) NSURL *URL;
@property (nonatomic, readonly, getter=isLoading) BOOL loading;
@property (nonatomic, readonly) double estimatedProgress;
@property (nonatomic, readonly) BOOL canGoBack;
@property (nonatomic, readonly) BOOL canGoForward;
@property (nullable, nonatomic, copy) NSString *customUserAgent;

- (instancetype)initWithFrame:(CGRect)frame configuration:(WKWebViewConfiguration *)configuration;
- (nullable WKNavigation *)loadRequest:(NSURLRequest *)request;
- (nullable WKNavigation *)loadHTMLString:(NSString *)string baseURL:(nullable NSURL *)baseURL;
- (nullable WKNavigation *)reload;
- (void)stopLoading;
- (nullable WKNavigation *)goBack;
- (nullable WKNavigation *)goForward;
- (void)evaluateJavaScript:(NSString *)javaScriptString completionHandler:(void (^)(id, NSError *))completionHandler;

@end

@interface WKUserContentController : NSObject
- (void)addScriptMessageHandler:(id)scriptMessageHandler name:(NSString *)name;
- (void)removeScriptMessageHandlerForName:(NSString *)name;
@end
