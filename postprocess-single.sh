#!/usr/bin/env bash
# Assemble cppsekai-standalone.html without ever holding the 22 MB asset pack
# as a Python string (that path produced a zero-filled file on this FUSE fs).
set -e
cd "$(dirname "$0")"
OUT="${1:-build-web}"

python3 - "$OUT" <<'PY'
import sys, pathlib
out = pathlib.Path(sys.argv[1])
shell = pathlib.Path('web/shell.html').read_text(encoding='utf-8')
head, tail = shell.split('{{{ SCRIPT }}}')
(tmp := pathlib.Path('/storage/emulated/0/cppsekai/.tmp')).mkdir(exist_ok=True)
(tmp / 'sh-head.html').write_text(head, encoding='utf-8')
(tmp / 'sh-tail.html').write_text(tail, encoding='utf-8')
js = (out / 'cppsekai-single.js').read_text(encoding='utf-8')
js = js.replace('</script', '<\\/script')
(tmp / 'app.js').write_text(js, encoding='utf-8')
print('prepared head/tail/app.js')
PY

base64 -w0 "$OUT/cppsekai-single.data" > /storage/emulated/0/cppsekai/.tmp/data.b64

T=/storage/emulated/0/cppsekai/.tmp
{
  cat "$T/sh-head.html"
  printf '<script>window.__CPPSEKAI_DATA_B64="'
  cat "$T/data.b64"
  printf '";</script>\n<script>\n'
  cat "$T/app.js"
  printf '\n</script>\n'
  cat "$T/sh-tail.html"
} > "$OUT/cppsekai-standalone.html.tmp"

# verify before replacing the real file
head -c 15 "$OUT/cppsekai-standalone.html.tmp" | grep -q '<!doctype html>' || { echo "bad head" >&2; exit 1; }
grep -q '__CPPSEKAI_DATA_B64' "$OUT/cppsekai-standalone.html.tmp" || { echo "missing data blob" >&2; exit 1; }
mv -f "$OUT/cppsekai-standalone.html.tmp" "$OUT/cppsekai-standalone.html"
ls -la "$OUT/cppsekai-standalone.html"
