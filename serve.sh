#!/usr/bin/env bash
# Serve the built web game over HTTP. file:// will NOT work (WebGL + fetch).
set -e
cd "$(dirname "$0")"
PORT="${PORT:-8765}"
DIR="${DIR:-build-web}"
if [ ! -f "$DIR/cppsekai.html" ]; then
    echo "No build in $DIR — run bash build-web.sh first." >&2
    exit 1
fi
IP=$(hostname -I 2>/dev/null | awk '{print $1}')
echo "Serving $DIR on http://0.0.0.0:$PORT/"
echo "  local : http://127.0.0.1:$PORT/cppsekai.html"
[ -n "$IP" ] && echo "  lan   : http://$IP:$PORT/cppsekai.html"
exec python3 -m http.server "$PORT" --bind 0.0.0.0 --directory "$DIR"
