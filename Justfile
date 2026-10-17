# Remove stray `tur` build artifacts that land in the repo root when tur is
# invoked from here (matches the patterns in .gitignore).
clean:
    rm -f *__*.c *__*.h *.so *.manifest event.c event.h tur.lock .DS_Store

# Render markdown guides under docs/guides/ to HTML (served at /guides/).
# The Markdown goes through tur/markdown (tools/mdrender.py, a copy of
# turmeric's), so this needs a tur: $TUR, vendor/tur/ from
# scripts/install-tur.sh, or one on PATH -- one whose stdlib has
# markdown/heading-html (turmeric main, or a release made after it).
guides:
    python3 tools/genguides.py docs/guides/ --out docs/html/guides/

# Generate per-spice HTML docs from this repo's spices/ directory.
# Depends on `guides` so the landing page can list guides alongside spices.
docs: guides
    python3 tools/genspices.py --out docs/html/ --emit-json docs/html/doc-names-spices.json

# Run the spices doc site locally via wrangler dev.
web-dev: docs
    rm -rf web/dist
    mkdir -p web/dist
    cp -R docs/html/. web/dist/
    cp -R web/public/. web/dist/
    cp -R web/styles web/dist/styles
    cd web && npm install && npx wrangler dev

# Deploy docs to spices.turmeric-lang.com via Cloudflare.
# Requires wrangler to be authenticated (wrangler login).
deploy-web: docs
    rm -rf web/dist
    mkdir -p web/dist
    cp -R docs/html/. web/dist/
    cp -R web/public/. web/dist/
    cp -R web/styles web/dist/styles
    cd web && npm install && npm run deploy

# Download and vendor a specific KaTeX release into the notebook spice.
# Usage: just vendor-katex 0.16.21
vendor-katex version:
    #!/usr/bin/env bash
    set -euo pipefail
    DEST="spices/notebook/src/notebook/vendor/katex"
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    curl -fsSL "https://registry.npmjs.org/katex/-/katex-{{version}}.tgz" -o "$TMP/katex.tgz"
    tar -xzf "$TMP/katex.tgz" -C "$TMP"
    mkdir -p "$DEST/fonts"
    cp "$TMP/package/dist/katex.min.js" "$DEST/katex.min.js"
    cp "$TMP/package/dist/katex.min.css" "$DEST/katex.min.css"
    cp "$TMP/package/dist/contrib/auto-render.min.js" "$DEST/auto-render.min.js"
    cp "$TMP/package/LICENSE" "$DEST/LICENSE"
    echo "{{version}}" > "$DEST/VERSION"
    cp "$TMP"/package/dist/fonts/*.woff2 "$DEST/fonts/"
    echo "Vendored KaTeX {{version}} into $DEST"

# Validate turmeric+sweet-exp code pairs in all spice READMEs.
check-docs:
    python3 tools/check-guide-pairs.py --spices

# Symlink vendor/tur/tur onto PATH at PREFIX/bin (default ~/.local/bin).
# Run `./scripts/install-tur.sh` first to populate vendor/tur/. The symlink
# keeps the binary next to its stdlib so the exe-walk resolver still works.
local-install prefix="~/.local":
    #!/usr/bin/env bash
    set -euo pipefail
    PREFIX="${prefix/#\~/$HOME}"
    SRC="$PWD/vendor/tur/tur"
    DEST="$PREFIX/bin/tur"
    if [ ! -x "$SRC" ]; then
        echo "error: $SRC not found -- run ./scripts/install-tur.sh first" >&2
        exit 1
    fi
    mkdir -p "$PREFIX/bin"
    ln -sf "$SRC" "$DEST"
    echo "linked $DEST -> $SRC"
    case ":$PATH:" in *":$PREFIX/bin:"*) ;; *)
        echo "note: $PREFIX/bin is not on PATH" >&2 ;;
    esac

# Remove the symlink created by `just local-install`.
local-uninstall prefix="~/.local":
    #!/usr/bin/env bash
    set -euo pipefail
    PREFIX="${prefix/#\~/$HOME}"
    DEST="$PREFIX/bin/tur"
    if [ -L "$DEST" ]; then
        rm "$DEST"
        echo "removed $DEST"
    elif [ -e "$DEST" ]; then
        echo "error: $DEST exists but is not a symlink -- leaving it alone" >&2
        exit 1
    else
        echo "nothing to remove at $DEST"
    fi
