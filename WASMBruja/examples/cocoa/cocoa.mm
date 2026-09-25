// Clean-room stand-in for Source/WTF/wtf/cocoa NSString and NSURL extras.
// brujac reads the IDL above the cpp marker; YetiKernel owns the C++ body.
interface NSStringExtras {
  boolean hasPrefix(DOMString string, DOMString prefix);
  boolean hasSuffix(DOMString string, DOMString suffix);
  DOMString stringByAppendingString(DOMString string, DOMString other);
  DOMString convertToASCIIUppercase(DOMString string);
  DOMString convertToASCIILowercase(DOMString string);
};

interface NSURLExtras {
  boolean isValid(DOMString spec);
  DOMString protocol(DOMString spec);
  DOMString host(DOMString spec);
  DOMString userVisibleString(DOMString spec);
};

// --- cpp ---
