// Force-link Emscripten's IDBFS backend.
//
// Two things are needed, and both were missing:
//   1. the identifier IDBFS must appear in compiled JS - naming it inside an
//      EM_ASM/EM_JS string does not count (those are runtime strings);
//   2. the function has to be *called* from C++, otherwise the jsify pass drops
//      this library entry (and its __deps) as dead code.
// platform/WebDownloader.cpp calls cppsekai_idbfs_supported() for exactly that.
mergeInto(LibraryManager.library, {
  cppsekai_idbfs_supported__deps: ['$IDBFS'],
  cppsekai_idbfs_supported: function () {
    return typeof IDBFS !== 'undefined' ? 1 : 0;
  },
});
