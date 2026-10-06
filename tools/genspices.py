#!/usr/bin/env python3
"""
tools/genspices.py -- Generate per-spice documentation pages.

For each spice in ./spices/<name>/ produces:
  docs/html/<name>/index.html  -- front page rendered from README.md
  docs/html/<name>/api/        -- auto-generated API reference from src/

Also produces the top-level index at docs/html/index.html with one row
per spice (tier, description, links to the front page and API reference).

Usage:
    python3 tools/genspices.py [--out docs/html/]
"""

import argparse
import html as html_module
import re
import sys
from pathlib import Path

import markdown as md_lib

sys.path.insert(0, str(Path(__file__).parent))
from genguides import (SIDEBAR_TOGGLE_JS, SYNTAX_TOGGLE_JS,
                       TURMERIC_HIGHLIGHT_JS, GUIDE_CSS,
                       inject_syntax_toggles, toc_tokens_to_sidebar)
from gendocs import render_tree, collect_doc_entries
from sitechrome import GITHUB_URL, build_page_header, build_sidebar

# Same repo URL the chrome's GitHub link uses -- one constant, so a transfer
# does not leave half the page pointing at the old owner.
GITHUB_BASE = GITHUB_URL
SPICES_REPO = Path('.')

PAGE_HEADER = build_page_header(active='Spices')

INDEX_TAB_JS = '''\
  <script>
  (function(){
    var tabs = document.querySelectorAll('.index-tab');
    if (!tabs.length) return;
    var panels = {
      spices: document.getElementById('panel-spices'),
      guides: document.getElementById('panel-guides'),
    };
    function activate(name){
      tabs.forEach(function(t){
        var on = t.dataset.tab === name;
        t.classList.toggle('active', on);
        t.setAttribute('aria-selected', on ? 'true' : 'false');
      });
      Object.keys(panels).forEach(function(k){
        if (panels[k]) panels[k].hidden = (k !== name);
      });
    }
    tabs.forEach(function(t){
      t.addEventListener('click', function(){ activate(t.dataset.tab); });
    });
    // Sidebar / in-page anchor links to #spices or #guides switch the tab too.
    document.querySelectorAll('a[href^="#"]').forEach(function(a){
      a.addEventListener('click', function(){
        var hash = a.getAttribute('href');
        if (hash === '#spices') activate('spices');
        else if (hash === '#guides') activate('guides');
      });
    });
  })();
  </script>'''


# ---------------------------------------------------------------------------
# Spice metadata
# ---------------------------------------------------------------------------

SpiceMeta = dict  # {name, description, tier, c_dep}


def discover_spices() -> list[Path]:
    """Return sorted spice directories under ./spices/."""
    root = SPICES_REPO / 'spices'
    if not root.is_dir():
        sys.exit(
            f'error: spices directory not found at {root.resolve()}. '
            f'Run from the turmeric-spices repo root.'
        )
    return sorted(p for p in root.iterdir() if p.is_dir())


def parse_readme_table(readme_text: str) -> dict[str, SpiceMeta]:
    """
    Parse the spices table in the top-level README.md and return a dict keyed
    by spice short-name (e.g. 'json' for 'tur-json').
    """
    rows: dict[str, SpiceMeta] = {}
    # Match table rows like: | [`tur-foo`](spices/foo/) | desc | tier | c dep |
    row_re = re.compile(
        r'^\|\s*\[`tur-([\w\-]+)`\]\(spices/[^)]+\)\s*\|'
        r'\s*([^|]+?)\s*\|'
        r'\s*([^|]+?)\s*\|'
        r'\s*([^|]+?)\s*\|',
        re.MULTILINE,
    )
    for m in row_re.finditer(readme_text):
        name, desc, tier, c_dep = m.groups()
        rows[name] = {
            'name': name,
            'description': desc.strip(),
            'tier': tier.strip(),
            'c_dep': c_dep.strip(),
        }
    return rows


def extract_build_description(build_tur: Path) -> str:
    """Read :description from a build.tur file, or return ''."""
    if not build_tur.is_file():
        return ''
    text = build_tur.read_text(encoding='utf-8', errors='replace')
    m = re.search(r':description\s+"([^"]+)"', text)
    return m.group(1) if m else ''


def _split_front_matter(text: str) -> tuple[dict, str]:
    """Return (meta, body) after stripping a leading YAML front-matter block.

    Guides that carry front matter used to leak the whole block into their
    landing-page summary, because the extractor below only skips blank and
    heading lines and a `---` fence is neither.
    """
    if not text.startswith('---\n'):
        return {}, text
    end = text.find('\n---', 4)
    if end == -1:
        return {}, text
    rest = text.find('\n', end + 1)
    body = text[rest + 1:] if rest != -1 else ''
    meta: dict = {}
    for line in text[4:end].splitlines():
        k, _, v = line.partition(':')
        k, v = k.strip(), v.strip()
        if k and v:
            meta[k] = v
    return meta, body


def _inline_md(text: str) -> str:
    """Render a summary's inline markdown (bold, italic, code) to HTML.

    Links are flattened to their anchor text on purpose: a guide's relative
    target is written relative to the guide, not to the index page that embeds
    the summary, so rendering it as an <a> would emit a broken link.
    """
    flat = re.sub(r'\[([^\]]+)\]\([^)]*\)', r'\1', text)
    out = md_lib.markdown(flat).strip()
    m = re.fullmatch(r'<p>(.*)</p>', out, re.S)
    return m.group(1).strip() if m else out


def _extract_guide_summary(text: str) -> str:
    """Pull the first meaningful sentence out of a guide's intro paragraph."""
    lines = text.splitlines()
    i = 0
    # Skip leading blank / heading lines
    while i < len(lines) and (not lines[i].strip() or lines[i].lstrip().startswith('#')):
        i += 1
    if i >= len(lines):
        return ''
    is_bq = lines[i].lstrip().startswith('>')
    para: list[str] = []
    while i < len(lines):
        s = lines[i].strip()
        if not s or s.startswith('#'):
            break
        if is_bq:
            if not s.startswith('>'):
                break
            para.append(s.lstrip('>').strip())
        else:
            if s.startswith('>'):
                break
            para.append(s)
        i += 1
    full = ' '.join(p for p in para if p).strip()
    # Strip "Spice version X.Y.Z -- " and an optional trailing "<name> <ver> "
    # stamp (e.g. "plutovg 1.3.3 ") so the summary starts with real prose.
    full = re.sub(r'^Spice version [^\s]+\s*--\s*(?:[\w\-]+\s+[\d.]+\s+)?',
                  '', full)
    # Prefer the first sentence with substance (skip short version-stamp fragments).
    for s in re.findall(r'.+?[.!?](?=\s|$)', full):
        s = s.strip()
        if len(s) >= 30:
            return s
    return full[:200].rstrip()


def discover_guides(guides_dir: Path) -> list[dict]:
    """
    Scan docs/guides/*.md and return a list of {stem, title, summary} entries
    for each guide, ordered by filename. Returns [] when the directory is
    absent so the landing page degrades gracefully.
    """
    if not guides_dir.is_dir():
        return []
    entries: list[dict] = []
    for md in sorted(guides_dir.glob('*.md')):
        if md.stem == 'README':
            continue
        meta, text = _split_front_matter(
            md.read_text(encoding='utf-8', errors='replace'))

        title = md.stem.replace('-', ' ').title()
        m = re.search(r'^#\s+(.+?)\s*$', text, re.MULTILINE)
        if m:
            heading = m.group(1).strip()
            title = re.sub(r'^tur-[\w\-]+\s*--\s*', '', heading).strip() or heading

        entries.append({
            'stem': md.stem,
            'title': meta.get('title') or title,
            'summary': meta.get('description') or _extract_guide_summary(text),
        })
    return entries


def collect_spice_meta(spice_dirs: list[Path],
                       table: dict[str, SpiceMeta]) -> list[SpiceMeta]:
    """Merge README table info with on-disk discovery, falling back to build.tur."""
    out: list[SpiceMeta] = []
    for d in spice_dirs:
        name = d.name
        meta = dict(table.get(name, {}))
        meta['name'] = name
        meta['path'] = d
        if not meta.get('description'):
            meta['description'] = extract_build_description(d / 'build.tur')
        meta.setdefault('tier', '--')
        meta.setdefault('c_dep', '--')
        out.append(meta)
    return out


# ---------------------------------------------------------------------------
# Per-spice front page
# ---------------------------------------------------------------------------

STUB_FRONT_PAGE = '''\
# tur-{name}

Docs in progress.

## See also

- [API reference](api/)
- Source: <{github}/tree/main/spices/{name}>
'''


def render_front_page(meta: SpiceMeta, out_dir: Path, style_rel: str) -> None:
    """Render docs/html/<name>/index.html from the spice's README.md."""
    out_dir.mkdir(parents=True, exist_ok=True)
    readme = meta['path'] / 'README.md'

    if readme.is_file():
        text = readme.read_text(encoding='utf-8')
        source_label = f'spices/{meta["name"]}/README.md'
    else:
        text = STUB_FRONT_PAGE.format(name=meta['name'], github=GITHUB_BASE)
        source_label = '(no README -- stub)'

    text = re.sub(r'^(`{3,})(turmeric|sweet-exp)\s+no-check\b[^\n]*', r'\1\2', text,
                  flags=re.MULTILINE)
    conv = md_lib.Markdown(
        extensions=['fenced_code', 'tables', 'toc'],
        extension_configs={'toc': {'permalink': False}},
    )
    body_html = conv.convert(text)
    body_html = inject_syntax_toggles(body_html)
    toc_tokens = getattr(conv, 'toc_tokens', [])

    sidebar_items = toc_tokens_to_sidebar(toc_tokens)
    sidebar_html = build_sidebar(
        toc=f'      <h3>On this page</h3>\n      <ul>{sidebar_items}</ul>',
        uplinks=[('api/', 'API reference'),
                 ('../index.html', 'All Spices')],
        extra_titles={
            'api/': f'Generated API reference for tur-{meta["name"]}',
            '../index.html': 'Every first-party spice, with tier and C dependency',
        },
    )

    title = f'tur-{meta["name"]} | Turmeric Spices'

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html_module.escape(title)}</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=DM+Sans:wght@300;400;500&display=swap" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/400.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/500.css" rel="stylesheet">
  <link rel="stylesheet" href="{style_rel}">
  <style>
{GUIDE_CSS}
  </style>
</head>
<body>
{PAGE_HEADER}
{SIDEBAR_TOGGLE_JS}
  <div class="page-layout">
    <div class="sidebar">
      {sidebar_html}
    </div>
    <div class="content guide-content">
      {body_html}
    </div>
  </div>
  <footer class="site-footer">
    Auto-generated by <code>tools/genspices.py (turmeric-spices)</code> &mdash; source: <code>{html_module.escape(source_label)}</code>
  </footer>
{TURMERIC_HIGHLIGHT_JS}
{SYNTAX_TOGGLE_JS}
</body>
</html>
'''
    (out_dir / 'index.html').write_text(html, encoding='utf-8')


# ---------------------------------------------------------------------------
# Per-spice API reference (delegates to gendocs.render_tree)
# ---------------------------------------------------------------------------

def render_api_reference(meta: SpiceMeta, out_dir: Path):
    """
    Generate the per-spice API tree under out_dir/api/.
    Returns the parsed module list (for collect_doc_entries), or None when
    the spice has no src/ directory.
    """
    src_dir = meta['path'] / 'src'
    if not src_dir.is_dir():
        return None
    api_out = out_dir / 'api'
    return render_tree(
        src_dir,
        api_out,
        brand=f'tur-{meta["name"]}',
        brand_label=f'tur-{meta["name"]} API',
    )


# ---------------------------------------------------------------------------
# Top-level spices index
# ---------------------------------------------------------------------------

def render_top_index(metas: list[SpiceMeta], out_dir: Path,
                     guides: list[dict] | None = None) -> None:
    """Render docs/html/index.html with a Guides section atop the spices list."""
    out_dir.mkdir(parents=True, exist_ok=True)
    guides = guides or []

    # ---- Guides section (rendered above the spices table) ----------------
    if guides:
        guide_rows = []
        for g in guides:
            stem = html_module.escape(g['stem'])
            title = html_module.escape(g['title'])
            summary = _inline_md(g['summary'])
            guide_rows.append(
                '      <tr>'
                f'<td><a href="guides/{stem}.html">{title}</a></td>'
                f'<td>{summary}</td>'
                '</tr>'
            )
        guides_table_html = (
            '<table class="guides-table">\n'
            '  <thead><tr><th>Guide</th><th>Summary</th></tr></thead>\n'
            '  <tbody>\n'
            + '\n'.join(guide_rows)
            + '\n  </tbody>\n</table>'
        )
        guides_section = (
            '<h2 id="guides">Guides</h2>\n'
            '<p>Long-form, task-oriented walkthroughs for individual spices. '
            'See the <a href="guides/">full guide index</a> for sidebar navigation.</p>\n'
            f'{guides_table_html}\n'
        )
    else:
        guides_section = ''

    # ---- Spices section --------------------------------------------------
    rows = []
    for meta in metas:
        name = meta['name']
        desc = meta.get('description', '') or ''
        tier = meta.get('tier', '--')
        c_dep = meta.get('c_dep', '--')
        rows.append(
            '      <tr>'
            f'<td><a href="{html_module.escape(name)}/"><code>tur-{html_module.escape(name)}</code></a></td>'
            f'<td>{html_module.escape(desc)}</td>'
            f'<td>{html_module.escape(tier)}</td>'
            f'<td>{html_module.escape(c_dep)}</td>'
            f'<td><a href="{html_module.escape(name)}/api/">API</a></td>'
            '</tr>'
        )
    table_html = (
        '<table class="spices-table">\n'
        '  <thead><tr>'
        '<th>Spice</th><th>Description</th><th>Tier</th><th>C dep</th><th>Docs</th>'
        '</tr></thead>\n'
        '  <tbody>\n'
        + '\n'.join(rows)
        + '\n  </tbody>\n</table>'
    )

    intro = (
        '<p>First-party spices for the Turmeric ecosystem. Each spice has its '
        'own docs -- click through for a front page and a per-spice API '
        'reference.</p>'
    )

    spices_section = (
        '<h2 id="spices">Spices</h2>\n'
        f'{intro}\n'
        f'{table_html}\n'
    )

    # ---- Tabbed layout: Spices first (default), Guides second -------------
    has_guides = bool(guides)
    if has_guides:
        tabs_html = (
            '<div class="index-tabs" role="tablist" aria-label="Spices index">\n'
            '  <button class="index-tab active" id="tab-spices" role="tab" '
            'aria-selected="true" aria-controls="panel-spices" '
            'data-tab="spices">Spices</button>\n'
            '  <button class="index-tab" id="tab-guides" role="tab" '
            'aria-selected="false" aria-controls="panel-guides" '
            'data-tab="guides">Guides</button>\n'
            '</div>\n'
        )
        body_content = (
            f'{tabs_html}'
            '<div class="index-panel" id="panel-spices" role="tabpanel" '
            'aria-labelledby="tab-spices">\n'
            f'{spices_section}'
            '</div>\n'
            '<div class="index-panel" id="panel-guides" role="tabpanel" '
            'aria-labelledby="tab-guides" hidden>\n'
            f'{guides_section}'
            '</div>\n'
        )
        tab_js = INDEX_TAB_JS
    else:
        body_content = spices_section
        tab_js = ''

    sidebar_links = ''
    if has_guides:
        sidebar_links = (
            '      <h3>On this page</h3>\n'
            '      <ul>\n'
            '        <li><a href="#spices">Spices</a></li>\n'
            '        <li><a href="#guides">Guides</a></li>\n'
            '      </ul>\n'
        )

    sidebar_html = build_sidebar(toc=sidebar_links.rstrip())

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Spices | Turmeric</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=DM+Sans:wght@300;400;500&display=swap" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/400.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/500.css" rel="stylesheet">
  <link rel="stylesheet" href="/styles/style.css">
  <style>
{GUIDE_CSS}
    .spices-table, .guides-table {{ width:100%; margin-top:1rem; }}
    .spices-table td code {{ font-size:0.85rem; }}
    .guides-table th:first-child, .guides-table td:first-child {{ white-space:nowrap; }}
    .index-tabs {{ display:flex; gap:2px; border-bottom:1px solid var(--border); margin-bottom:1.5rem; }}
    .index-tab {{ padding:0.5rem 1.1rem; background:transparent; color:var(--text-sec); border:none; border-bottom:2px solid transparent; cursor:pointer; font-size:0.95rem; font-family:inherit; transition:all 0.14s; }}
    .index-tab:hover {{ color:var(--text-primary); }}
    .index-tab.active {{ color:var(--gold-bright); border-bottom-color:var(--gold); }}
    .index-panel h2:first-child {{ margin-top:0; }}
  </style>
</head>
<body>
{PAGE_HEADER}
{SIDEBAR_TOGGLE_JS}
  <div class="page-layout">
    <div class="sidebar">
      {sidebar_html}
    </div>
    <div class="content guide-content">
      <h1>Turmeric Spices</h1>
      {body_content}
    </div>
  </div>
  <footer class="site-footer">
    Auto-generated by <code>tools/genspices.py (turmeric-spices)</code>
  </footer>
{TURMERIC_HIGHLIGHT_JS}
{SYNTAX_TOGGLE_JS}
{tab_js}
</body>
</html>
'''
    (out_dir / 'index.html').write_text(html, encoding='utf-8')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    import json

    p = argparse.ArgumentParser(description='Generate per-spice doc pages.')
    p.add_argument('--out', default='docs/html/', help='Output directory')
    p.add_argument(
        '--emit-json',
        metavar='PATH',
        help='Write a JSON array of {name, summary, kind, spice} entries to PATH. '
             'Served as doc-names-spices.json for the web REPL symbol search.',
    )
    args = p.parse_args()

    out_dir = Path(args.out)

    spice_dirs = discover_spices()
    readme_path = SPICES_REPO / 'README.md'
    table = parse_readme_table(readme_path.read_text(encoding='utf-8')) \
        if readme_path.is_file() else {}
    metas = collect_spice_meta(spice_dirs, table)

    print(f'Generating docs for {len(metas)} spices into {out_dir}/')
    all_entries: list[dict] = []
    for meta in metas:
        print(f'-> {meta["name"]}')
        spice_out = out_dir / meta['name']
        # Front page links to /styles/style.css (deployed alongside the docs tree)
        render_front_page(meta, spice_out, style_rel='/styles/style.css')
        modules = render_api_reference(meta, spice_out)
        if modules is None:
            print(f'   (no src/ directory; skipping API reference)')
            continue
        all_entries.extend(collect_doc_entries(modules, spice=meta['name']))

    guides = discover_guides(SPICES_REPO / 'docs' / 'guides')
    if guides:
        print(f'Found {len(guides)} guide(s) for landing-page Guides section')
    render_top_index(metas, out_dir, guides=guides)
    if args.emit_json:
        out_json = Path(args.emit_json)
        out_json.parent.mkdir(parents=True, exist_ok=True)
        out_json.write_text(
            json.dumps(all_entries, ensure_ascii=True, indent=None,
                       separators=(',', ':')),
            encoding='utf-8',
        )
        print(f'Wrote {out_json} ({len(all_entries)} spice doc entries)')
    print(f'Done: {out_dir / "index.html"}')


if __name__ == '__main__':
    main()
