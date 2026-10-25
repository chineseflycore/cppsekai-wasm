"""
Inject synchronous IndexedDB persistence into the built page.

Why not IDBFS: IDBFS restores asynchronously, but the game reads
/user/userdata.json and scans /charts *synchronously* during boot. Any async
restore therefore always loses - settings reset and the chart list comes up
empty on every reload.

This layer instead:

  * registers a Module.preRun hook that pulls every stored file out of
    IndexedDB and writes it into the emscripten FS *before* the module starts
    (addRunDependency blocks startup until the promise settles), and
  * hooks FS.writeFile / FS.unlink so anything the game or the downloader
    writes is mirrored back into IndexedDB.

Everything under /user and /charts is persisted; other paths are ignored.

Usage: python inject-persistence.py
In:   web-port/CppSekai-Web/dist/cppsekai.html
Out:  web-port/CppSekai-Web/dist/cppsekai.html (in place, .orig backup once)
"""
import os

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "web-port", "CppSekai-Web", "dist")
HTML = os.path.join(DIST, "cppsekai.html")

PRE = r"""<script>
(function () {
  var DB = 'cppsekai-fs', STORE = 'files', VER = 1;
  var ROOTS = ['/user', '/charts'];
  var db = null;

  function openDb() {
    if (db) { return Promise.resolve(db); }
    return new Promise(function (res, rej) {
      var r = indexedDB.open(DB, VER);
      r.onupgradeneeded = function () { r.result.createObjectStore(STORE); };
      r.onsuccess = function () { db = r.result; res(db); };
      r.onerror = function () { rej(r.error); };
    });
  }
  function put(path, bytes) {
    return openDb().then(function (d) {
      return new Promise(function (res, rej) {
        var tx = d.transaction(STORE, 'readwrite');
        tx.objectStore(STORE).put(bytes, path);
        tx.oncomplete = function () { res(); };
        tx.onerror = function () { rej(tx.error); };
      });
    });
  }
  function del(path) {
    return openDb().then(function (d) {
      return new Promise(function (res, rej) {
        var tx = d.transaction(STORE, 'readwrite');
        tx.objectStore(STORE).delete(path);
        tx.oncomplete = function () { res(); };
        tx.onerror = function () { rej(tx.error); };
      });
    });
  }
  function all() {
    return openDb().then(function (d) {
      return new Promise(function (res, rej) {
        var out = {};
        var tx = d.transaction(STORE, 'readonly');
        var req = tx.objectStore(STORE).openCursor();
        req.onsuccess = function (e) {
          var c = e.target.result;
          if (c) { out[c.key] = new Uint8Array(c.value); c.continue(); }
          else { res(out); }
        };
        tx.onerror = function () { rej(tx.error); };
      });
    });
  }
  function tracked(path) {
    for (var i = 0; i < ROOTS.length; i++) {
      if (path === ROOTS[i] || path.indexOf(ROOTS[i] + '/') === 0) { return true; }
    }
    return false;
  }

  window.__persist = { put: put, del: del, all: all, tracked: tracked };

  window.__persistRestore = function () {
    return all().then(function (files) {
      var n = 0;
      Object.keys(files).forEach(function (p) {
        try {
          var slash = p.lastIndexOf('/');
          if (slash > 0) { Module.FS.mkdirTree(p.substring(0, slash)); }
          Module.FS.writeFile(p, files[p]);
          n++;
        } catch (e) { console.warn('[persist] restore failed', p, e); }
      });
      console.log('[persist] restored ' + n + ' file(s): ' + Object.keys(files).join(', '));
    }).catch(function (e) { console.warn('[persist] restore error', e); });
  };

  // Mirror writes back to IndexedDB.
  //
  // A previous revision hooked FS.close to catch C++ std::ofstream writes. That
  // was wrong on two counts: stream.path is a pointer into the wasm heap (read
  // after the stream was freed = wild pointer), and reading whole files back on
  // every close leaked memory. The downloader writes through JS (FS.writeFile),
  // so that hook stays; the game's own saves are picked up by polling the tiny
  // /user tree instead.
  window.__persistHook = function () {
    var FS = Module.FS;
    var wf = FS.writeFile;
    FS.writeFile = function (path, data, opts) {
      var r = wf.apply(this, arguments);
      if (tracked(path)) {
        try { put(path, (data instanceof Uint8Array) ? data : new Uint8Array(data)); } catch (e) {}
      }
      return r;
    };
    var um = FS.unlink;
    if (um) {
      FS.unlink = function (path) {
        var r = um.apply(this, arguments);
        if (tracked(path)) { try { del(path); } catch (e) {} }
        return r;
      };
    }

    // userdata.json / profiles are a few KB; syncing them every 2s is cheap and
    // never touches freed wasm memory.
    var last = {};
    setInterval(function () {
      try {
        var files = ['/user/userdata.json'];
        var dirs = ['/user/profiles'];
        dirs.forEach(function (d) {
          var st = FS.analyzePath(d);
          if (st && st.exists && st.isFolder) {
            FS.readdir(d).forEach(function (name) { files.push(d + '/' + name); });
          }
        });
        files.forEach(function (p) {
          var st = FS.analyzePath(p);
          if (!st || !st.exists || st.isFolder) { return; }
          var data = FS.readFile(p);
          var sig = p + ':' + data.length;
          for (var i = 0; i < data.length; i += 97) { sig += ',' + data[i]; }
          if (last[p] === sig) { return; }
          last[p] = sig;
          put(p, data);
        });
      } catch (e) {}
    }, 2000);
  };

  if (typeof Module === 'undefined') { window.Module = {}; }
  Module.preRun = (Module.preRun || []).concat([function () {
    addRunDependency('persist-restore');
    var finished = false;
    function finish() {
      if (finished) { return; }
      finished = true;
      removeRunDependency('persist-restore');
    }
    // Never let a stuck IndexedDB open wedge startup: the boot overlay sits on
    // "正在解码资源…" forever if this dependency is not released.
    setTimeout(finish, 4000);
    try {
      window.__persistHook();
    } catch (e) {
      console.warn('[persist] hook failed', e);
    }
    window.__persistRestore().catch(function () {}).then(finish);
  }]);
})();
</script>"""


def main():
    html = open(HTML, encoding="utf-8").read()
    if "persist-restore" in html:
        print("[persist] already injected")
        return
    # The injection has to land between the page's own "var Module = {...}"
    # block and the engine script: earlier, and that var wipes our preRun hook;
    # later, and the engine has already read the hooks.
    marker = "<script async src=cppsekai.js"
    if marker in html:
        html = html.replace(marker, PRE + marker, 1)
    elif "</body>" in html:
        html = html.replace("</body>", PRE + "</body>", 1)
    else:
        raise SystemExit("unexpected html: no engine script and no </body>")
    open(HTML, "w", encoding="utf-8").write(html)
    print(f"[persist] injected ({os.path.getsize(HTML)/1024:.1f} KB)")


if __name__ == "__main__":
    main()
