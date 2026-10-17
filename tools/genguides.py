#!/usr/bin/env python3
"""
tools/genguides.py -- Render Turmeric guide markdown files to HTML.

Usage:
    python3 tools/genguides.py docs/guides/ [--out docs/html/guides/]
"""

import argparse
import html as _html
import re
import subprocess
import sys
from pathlib import Path

import markdown as md_lib

# Topbar and sidebar globals come from the one chrome module, so the guides,
# the spice front pages and the API trees all name the same set of places --
# and name them the way the main site does.  See tools/sitechrome.py.
from sitechrome import build_page_header, build_sidebar


def parse_front_matter(text: str) -> tuple[dict, str]:
    """Return (meta_dict, body) after stripping YAML front matter (--- blocks)."""
    if not text.startswith('---\n'):
        return {}, text
    end = text.find('\n---', 4)
    if end == -1:
        return {}, text
    fm_text = text[4:end]
    rest_start = text.find('\n', end + 1)
    body = text[rest_start + 1:] if rest_start != -1 else ''
    meta: dict = {}
    for line in fm_text.splitlines():
        if ':' in line:
            k, _, v = line.partition(':')
            k, v = k.strip(), v.strip()
            if k:
                meta[k] = v
    return meta, body


def build_categories_from_meta(meta_by_stem: dict, all_stems: set) -> list:
    """Build ordered category list from guide front matter."""
    buckets: dict[str, list] = {}
    for stem in sorted(all_stems):
        meta = meta_by_stem.get(stem, {})
        cat = meta.get('category', '').strip() or 'Other'
        title = meta.get('title', stem.replace('-', ' ').title()).strip()
        desc = meta.get('description', '').strip()
        buckets.setdefault(cat, []).append({'stem': stem, 'label': title, 'desc': desc})
    cat_names = sorted(k for k in buckets if k != 'Other')
    if 'Other' in buckets:
        cat_names.append('Other')
    return [{'name': name, 'guides': buckets[name]} for name in cat_names]


STYLE_REL = '/styles/style.css'

PAGE_HEADER = build_page_header(active='Spices')

SIDEBAR_TOGGLE_JS = '''\
  <div class="sidebar-overlay"></div>
  <script>
    document.addEventListener('DOMContentLoaded', function(){
      var btn = document.querySelector('.hamburger');
      var sidebar = document.querySelector('.sidebar');
      var overlay = document.querySelector('.sidebar-overlay');
      if (!btn || !sidebar) return;
      function open() { sidebar.classList.add('is-open'); overlay && overlay.classList.add('is-open'); }
      function close() { sidebar.classList.remove('is-open'); overlay && overlay.classList.remove('is-open'); }
      btn.addEventListener('click', function(){ sidebar.classList.contains('is-open') ? close() : open(); });
      overlay && overlay.addEventListener('click', close);
    });
  </script>'''

RECENT_TABS_JS = '''\
  <script>
  document.addEventListener('DOMContentLoaded', function(){
    var btns = Array.prototype.slice.call(
      document.querySelectorAll('.recent-tablist .seg-btn'));
    function select(slug, focus) {
      if (!btns.some(function(b){ return b.dataset.recentTab === slug; })) return false;
      btns.forEach(function(b){
        var on = b.dataset.recentTab === slug;
        b.classList.toggle('active', on);
        b.setAttribute('aria-selected', on ? 'true' : 'false');
        b.tabIndex = on ? 0 : -1;
        var panel = document.getElementById(b.dataset.recentTab);
        if (panel) panel.hidden = !on;
        if (on && focus) b.focus();
      });
      return true;
    }
    btns.forEach(function(b, i){
      b.addEventListener('click', function(){ select(b.dataset.recentTab, false); });
      // Arrow / Home / End move between tabs (WAI-ARIA tabs pattern).
      b.addEventListener('keydown', function(e){
        var n = btns.length, j = -1;
        if (e.key === 'ArrowRight') j = (i + 1) % n;
        else if (e.key === 'ArrowLeft') j = (i - 1 + n) % n;
        else if (e.key === 'Home') j = 0;
        else if (e.key === 'End') j = n - 1;
        if (j < 0) return;
        e.preventDefault();
        select(btns[j].dataset.recentTab, true);
      });
    });
    // The sidebar's #recently-added / #recently-updated links pick the tab.
    function fromHash() { select(location.hash.slice(1), false); }
    window.addEventListener('hashchange', fromHash);
    fromHash();
  });
  </script>'''


SYNTAX_TOGGLE_JS = '''\
  <script>
  (function(){
    function applyToggle(toggle, syntax) {
      var card = toggle.closest('.code-card');
      if (!card) return;
      toggle.querySelectorAll('.seg-btn').forEach(function(btn){
        var active = btn.dataset.syntax === syntax;
        btn.classList.toggle('active', active);
        btn.setAttribute('aria-selected', active ? 'true' : 'false');
      });
      card.querySelectorAll('.code-version').forEach(function(v){
        v.style.display = v.classList.contains(syntax + '-version') ? '' : 'none';
      });
    }

    // ST1.5: restore stored preference across all cards on load
    var stored = localStorage.getItem('guide-syntax');
    if (stored) {
      document.querySelectorAll('.code-syntax-toggle').forEach(function(t){ applyToggle(t, stored); });
    }

    document.querySelectorAll('.code-syntax-toggle').forEach(function(toggle){
      // Click handler
      toggle.addEventListener('click', function(e){
        if (!e.target.classList.contains('seg-btn')) return;
        var syntax = e.target.dataset.syntax;
        document.querySelectorAll('.code-syntax-toggle').forEach(function(t){ applyToggle(t, syntax); });
        localStorage.setItem('guide-syntax', syntax);
      });
      // ST5.2: arrow-key navigation within the tablist
      toggle.addEventListener('keydown', function(e){
        var btns = Array.from(toggle.querySelectorAll('.seg-btn'));
        var idx = btns.indexOf(document.activeElement);
        if (idx === -1) return;
        if (e.key === 'ArrowRight'){ btns[(idx+1)%btns.length].focus(); e.preventDefault(); }
        if (e.key === 'ArrowLeft') { btns[(idx-1+btns.length)%btns.length].focus(); e.preventDefault(); }
      });
    });
  })();
  </script>'''

TURMERIC_HIGHLIGHT_JS = '''\
  <script>
  (function(){
    var KW = new Set([
      'defn','defmacro','defstruct','definstance','defdata','defgadt','defclass','def','let','let*','letrec',
      'if','cond','when','unless','do','begin','and','or','not',
      'fn','lambda','async','await','match','case',
      'quote','quasiquote','unquote','for','while','loop','do-m',
      'set!','try','catch','finally','with','use',
      'import','export','module','require','provide',
      'cons','car','cdr','nil-value','some','none','ok','err',
      'map','filter','reduce','apply','return','yield','raise','throw',
      'coerce','cast','type-of','any',
    ]);
    function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
    function hl(code){
      var out='', i=0, n=code.length;
      while(i<n){
        var c=code[i];
        // Line comment
        if(c===';'){
          var e=code.indexOf('\\n',i); if(e===-1)e=n;
          out+='<span class="hl-comment">'+esc(code.slice(i,e))+'</span>'; i=e; continue;
        }
        // String
        if(c==='"'){
          var j=i+1;
          while(j<n){if(code[j]==='\\\\'){j+=2;continue;}if(code[j]==='"'){j++;break;}j++;}
          out+='<span class="hl-string">'+esc(code.slice(i,j))+'</span>'; i=j; continue;
        }
        // Type annotation :keyword
        if(c===':'&&i+1<n&&/[a-zA-Z_]/.test(code[i+1])){
          var j=i+1;
          while(j<n&&/[a-zA-Z0-9_\\-?!]/.test(code[j]))j++;
          out+='<span class="hl-type">'+esc(code.slice(i,j))+'</span>'; i=j; continue;
        }
        // Number (integer or float, possibly negative)
        if(/[0-9]/.test(c)||(c==='-'&&i+1<n&&/[0-9]/.test(code[i+1]))){
          var j=i; if(code[j]==='-')j++;
          while(j<n&&/[0-9a-fA-FxX_\\.]/.test(code[j]))j++;
          out+='<span class="hl-number">'+esc(code.slice(i,j))+'</span>'; i=j; continue;
        }
        // Symbol / identifier
        if(/[a-zA-Z_\\-!?*+<>=\\/&%^~#@]/.test(c)){
          var j=i;
          while(j<n&&/[a-zA-Z0-9_\\-!?*+<>=\\/&%^~#@\\.]/.test(code[j]))j++;
          var sym=code.slice(i,j);
          if(sym==='true'||sym==='false'||sym==='nil'){
            out+='<span class="hl-number">'+esc(sym)+'</span>';
          } else if(KW.has(sym)){
            out+='<span class="hl-keyword">'+esc(sym)+'</span>';
          } else {
            out+=esc(sym);
          }
          i=j; continue;
        }
        out+=esc(c); i++;
      }
      return out;
    }
    document.querySelectorAll('pre code.language-turmeric, pre code.language-sweet-exp').forEach(function(el){
      el.innerHTML=hl(el.textContent);
    });
  })();
  </script>'''

GUIDE_CSS = '''\
    .guide-content h1 { font-size:1.75rem; color:var(--gold-bright); margin-bottom:1.5rem; padding-bottom:0.75rem; border-bottom:1px solid var(--border); }
    .guide-content h2 { font-size:1.2rem; color:var(--text-primary); margin:2rem 0 0.75rem; }
    .guide-content h3 { font-size:1rem; color:var(--gold-bright); margin:1.5rem 0 0.5rem; }
    .guide-content p  { margin-bottom:1rem; }
    .guide-content ul, .guide-content ol { margin:0 0 1rem 1.5rem; }
    .guide-content li { margin:0.25rem 0; }
    .guide-content code { font-family:"Iosevka","Fira Code",monospace; font-size:0.85em; background:var(--bg-panel); border:1px solid var(--border); border-radius:3px; padding:0.1em 0.35em; }
    .guide-content pre { background:var(--bg-panel); border:1px solid var(--border); border-radius:4px; padding:1rem; overflow-x:auto; margin-bottom:1rem; }
    .guide-content pre code { background:none; border:none; padding:0; font-size:0.85rem; }
    .guide-content blockquote { border-left:3px solid var(--gold); padding-left:1rem; color:var(--text-sec); margin:1rem 0; }
    .guide-content table { border-collapse:collapse; width:100%; margin-bottom:1rem; font-size:0.9rem; }
    .guide-content th { background:var(--bg-surface); border:1px solid var(--border); padding:0.5rem 0.75rem; text-align:left; color:var(--gold-bright); }
    .guide-content td { border:1px solid var(--border); padding:0.5rem 0.75rem; }
    .guide-content a { color:var(--gold-bright); }
    .guide-content strong { color:var(--text-primary); }
    .hl-comment { color:#48433D; font-style:italic; }
    .hl-string  { color:#D9735A; }
    .hl-number  { color:#A8C98A; }
    .hl-keyword { color:#EFA030; font-weight:bold; }
    .hl-type    { color:#7AC4B8; }
    .code-toggle { border:1px solid var(--border); border-radius:4px; margin-bottom:1rem; overflow:hidden; }
    .code-card-bar { background:var(--bg-surface); border-bottom:1px solid var(--border); padding:0.35rem 0.75rem; display:flex; align-items:center; }
    .code-syntax-toggle { margin-left:auto; display:flex; border:1px solid var(--border); border-radius:4px; overflow:hidden; font-family:"Iosevka","Fira Code",monospace; font-size:11px; }
    .seg-btn { padding:3px 10px; background:transparent; color:var(--text-sec); border:none; cursor:pointer; }
    .seg-btn:hover { color:var(--text-primary); }
    .seg-btn.active { color:var(--gold-bright); background:var(--bg-hover); }
    .code-card-body { }
    .code-version { }
    .guide-content .code-toggle pre { border:none; border-radius:0; margin-bottom:0; }'''


def inject_syntax_toggles(body_html: str) -> str:
    """Wrap adjacent turmeric+sweet-exp block pairs in a syntax-toggle widget."""
    # The `(?:(?!</code></pre>).)*` is a tempered dot, not decoration: a plain
    # `.*?` here backtracks PAST its own block's close when the next sibling is
    # not a sweet-exp block, swallowing the prose and code blocks in between
    # until it reaches a turmeric block that IS followed by one. The widget then
    # hid that prose inside the code card, and toggling to sweet-exp made it
    # vanish. Tempering keeps each group inside a single <pre>.
    pattern = re.compile(
        r'(<pre><code class="language-turmeric">(?:(?!</code></pre>).)*</code></pre>)'
        r'(\s*)'
        r'(<pre><code class="language-sweet-exp">(?:(?!</code></pre>).)*</code></pre>)',
        re.DOTALL,
    )

    def wrap_pair(m: re.Match) -> str:
        tur_block = m.group(1)
        sweet_block = m.group(3)
        return (
            '<div class="code-card code-toggle">'
            '<div class="code-card-bar">'
            '<div class="code-syntax-toggle" role="tablist">'
            '<button class="seg-btn active" data-syntax="turmeric"'
            ' role="tab" aria-selected="true">turmeric</button>'
            '<button class="seg-btn" data-syntax="sweet-exp"'
            ' role="tab" aria-selected="false">sweet-exp</button>'
            '</div>'
            '</div>'
            '<div class="code-card-body">'
            f'<div class="code-version turmeric-version" role="tabpanel">{tur_block}</div>'
            f'<div class="code-version sweet-exp-version" role="tabpanel"'
            f' style="display:none">{sweet_block}</div>'
            '</div>'
            '</div>'
        )

    return pattern.sub(wrap_pair, body_html)


def toc_tokens_to_sidebar(tokens: list) -> str:
    """Recursively render toc_tokens into sidebar <li> elements."""
    items = []
    for tok in tokens:
        anchor = tok.get('id', '')
        name = tok.get('name', '')
        level = tok.get('level', 2)
        indent = 'padding-left:0.75rem;' if level > 2 else ''
        color = 'color:var(--text-sec);' if level > 2 else ''
        items.append(
            f'<li style="{indent}"><a href="#{anchor}" style="{color}">{name}</a></li>'
        )
        children = tok.get('children', [])
        if children:
            items.append(toc_tokens_to_sidebar(children))
    return '\n'.join(items)


def render_guide(stem: str, src: Path, out: Path, all_stems: set, meta: dict | None = None) -> None:
    raw = src.read_text(encoding='utf-8')
    fm_meta, text = parse_front_matter(raw)
    if meta is None:
        meta = fm_meta

    # Rewrite .md links to .html (only local, non-absolute links)
    def rewrite_md_link(m: re.Match) -> str:
        href = m.group(1)
        if href.startswith('http') or href.startswith('/') or href.startswith('..'):
            return m.group(0)
        return f'href="{Path(href).stem}.html"'

    text = re.sub(r'href="([^"]+\.md)"', rewrite_md_link, text)
    text = re.sub(r'^(`{3,})(turmeric|sweet-exp)\s+no-check\b[^\n]*', r'\1\2', text,
                  flags=re.MULTILINE)

    conv = md_lib.Markdown(extensions=['fenced_code', 'tables', 'toc'],
                            extension_configs={'toc': {'permalink': False}})
    body_html = conv.convert(text)
    body_html = inject_syntax_toggles(body_html)
    toc_tokens = getattr(conv, 'toc_tokens', [])

    fm_title = meta.get('title', '').strip() if meta else ''
    if fm_title:
        title = fm_title
    else:
        title_match = re.match(r'^#\s+(.+)', text, re.MULTILINE)
        title = title_match.group(1) if title_match else stem.replace('-', ' ').title()

    sidebar_items = toc_tokens_to_sidebar(toc_tokens)
    sidebar_html = build_sidebar(
        toc=f'      <h3>On this page</h3>\n      <ul>{sidebar_items}</ul>',
        uplinks=[('index.html', 'All Guides')],
        extra_titles={'index.html': 'Every spice guide, grouped by category'},
    )

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title} | Turmeric Guides</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=DM+Sans:wght@300;400;500&display=swap" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/400.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/500.css" rel="stylesheet">
  <link rel="stylesheet" href="{STYLE_REL}">
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
    Auto-generated by <code>tools/genguides.py</code> &mdash; source: <code>docs/guides/{stem}.md</code>
  </footer>
{TURMERIC_HIGHLIGHT_JS}
{SYNTAX_TOGGLE_JS}
</body>
</html>
'''
    out.write_text(html, encoding='utf-8')
    print(f'  {stem}.html')


def _git_dates(path: Path, repo_root: Path) -> tuple[str | None, str | None]:
    """Return (added, updated) as YYYY-MM-DD from git history, None when unknown.

    `added` is the commit that first added the file (following renames);
    `updated` is the newest commit touching it. Both are None outside a git
    checkout, so the index simply omits the Recently tabs there.
    """
    def log(*args: str) -> list[str]:
        try:
            r = subprocess.run(['git', 'log', '--format=%ad', '--date=short',
                                *args, '--', str(path.resolve())],
                               cwd=repo_root, capture_output=True, text=True,
                               check=False)
        except FileNotFoundError:
            return []
        return [ln.strip() for ln in r.stdout.splitlines() if ln.strip()] \
            if r.returncode == 0 else []

    added = log('--diff-filter=A', '--follow')
    latest = log('-1')
    return (added[-1] if added else None, latest[0] if latest else None)



def _fmt_desc(text: str) -> str:
    """Normalize and render a guide description for the index page.

    - Replaces em dashes with '--'
    - HTML-escapes special characters
    - Converts backtick spans to <code> elements
    """
    text = text.replace('—', '--')
    text = _html.escape(text)
    text = re.sub(r'`([^`]+)`', lambda m: f'<code>{m.group(1)}</code>', text)
    return text


def render_index(categories: list[dict], all_stems: set[str], out_dir: Path,
                 recent: list[dict] | None = None,
                 recent_updated: list[dict] | None = None) -> None:
    recent = recent or []
    recent_updated = recent_updated or []
    categorized_stems = {g['stem'] for c in categories for g in c['guides']}

    def guide_item(g: dict) -> str:
        if g['stem'] not in all_stems:
            return ''
        return (f'<li><a href="{g["stem"]}.html">{g["label"]}</a>'
                f'<span style="color:var(--text-sec)"> -- {_fmt_desc(g["desc"])}</span></li>')

    cards = []
    for cat in categories:
        items = [s for g in cat['guides'] if (s := guide_item(g))]
        if not items:
            continue
        slug = re.sub(r'\s+', '-', cat['name'].lower())
        cards.append(f'''\
    <div class="index-card" style="display:block" id="{slug}">
      <h3 style="font-family:system-ui;font-size:0.9rem;margin-bottom:0.5rem">{cat['name']}</h3>
      <ul style="list-style:none;margin:0">
        {"".join(items)}
      </ul>
    </div>''')

    uncategorized = sorted(all_stems - categorized_stems)
    if uncategorized:
        items = [f'<li><a href="{s}.html">{s}</a></li>'
                 for s in uncategorized]
        cards.append(f'''\
    <div class="index-card" style="display:block">
      <h3 style="font-family:system-ui;font-size:0.9rem;margin-bottom:0.5rem">Other</h3>
      <ul style="list-style:none;margin:0">{"".join(items)}</ul>
    </div>''')

    # Recently Added / Recently Updated, as two tabs of one card. A guide whose
    # newest commit is the one that added it is new, not updated, so it appears
    # only under Recently Added. The first tab is selected server-side, so the
    # page reads correctly with no JS at all.
    tabs = [(slug, heading, entries) for slug, heading, entries in (
        ('recently-added', 'Recently Added', recent),
        ('recently-updated', 'Recently Updated', recent_updated),
    ) if entries]

    def dated_items(entries: list[dict]) -> str:
        return ''.join(
            f'<li><a href="{r["stem"]}.html">{_html.escape(r["label"])}</a>'
            f'<span style="color:var(--text-sec)"> -- {r["date"]}</span></li>'
            for r in entries)

    buttons = ''.join(
        f'<button class="seg-btn{" active" if i == 0 else ""}" role="tab"'
        f' id="{slug}-tab" data-recent-tab="{slug}" aria-controls="{slug}"'
        f' aria-selected="{"true" if i == 0 else "false"}"'
        f' tabindex="{0 if i == 0 else -1}">{heading}</button>'
        for i, (slug, heading, _) in enumerate(tabs))
    panels = ''.join(
        f'<ul class="recent-panel" role="tabpanel" id="{slug}"'
        f' aria-labelledby="{slug}-tab" style="list-style:none;margin:0"'
        f'{"" if i == 0 else " hidden"}>{dated_items(entries)}</ul>'
        for i, (slug, _, entries) in enumerate(tabs))
    recent_html = f'''\
      <div class="index-card recent-tabs" style="display:block;margin-bottom:1.5rem">
        <div class="recent-tablist" role="tablist" aria-label="Recent guide changes">{buttons}</div>
        {panels}
      </div>''' if tabs else ''

    sidebar_cats = '\n'.join(
        f'<li><a href="#{re.sub(r" +", "-", c["name"].lower())}">{c["name"]}</a></li>'
        for c in categories if any(g['stem'] in all_stems for g in c['guides'])
    )
    recent_links = ''.join(
        f'<li><a href="#{slug}">{heading}</a></li>' for slug, heading, _ in tabs)
    index_sidebar = build_sidebar(
        toc=f'      <h3>Categories</h3>\n      <ul>{recent_links}{sidebar_cats}</ul>')

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Guides | Turmeric</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=DM+Sans:wght@300;400;500&display=swap" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/400.css" rel="stylesheet">
  <link href="https://cdn.jsdelivr.net/npm/@fontsource/iosevka@5/500.css" rel="stylesheet">
  <link rel="stylesheet" href="{STYLE_REL}">
  <style>
    .index-card ul li {{ margin:0.3rem 0; font-size:0.875rem; }}
    .index-card ul li a {{ color:var(--text-primary); }}
    .index-card ul li a:hover {{ color:var(--gold); }}
    .recent-tablist {{ display:inline-flex; border:1px solid var(--border); border-radius:4px; overflow:hidden; margin-bottom:0.6rem; font-family:system-ui; font-size:0.8rem; }}
    .recent-tablist .seg-btn {{ padding:4px 12px; background:transparent; color:var(--text-sec); border:none; cursor:pointer; font:inherit; }}
    .recent-tablist .seg-btn + .seg-btn {{ border-left:1px solid var(--border); }}
    .recent-tablist .seg-btn:hover {{ color:var(--text-primary); }}
    .recent-tablist .seg-btn.active {{ color:var(--gold-bright); background:var(--bg-hover); }}
    .recent-tablist .seg-btn:focus-visible {{ outline:2px solid var(--gold); outline-offset:-2px; }}
  </style>
</head>
<body>
{PAGE_HEADER}
{SIDEBAR_TOGGLE_JS}
  <div class="page-layout">
    <div class="sidebar">
{index_sidebar}
    </div>
    <div class="content">
      <div class="module-heading">
        <h1 style="font-family:system-ui;color:var(--gold)">Guides</h1>
        <div class="module-path">Tutorials, how-tos, and in-depth feature guides for Turmeric</div>
      </div>
      {recent_html}
      <div class="index-grid">
        {"".join(cards)}
      </div>
    </div>
  </div>
  <footer class="site-footer">
    Auto-generated by <code>tools/genguides.py</code>
  </footer>
{TURMERIC_HIGHLIGHT_JS}
{RECENT_TABS_JS}
</body>
</html>
'''
    (out_dir / 'index.html').write_text(html, encoding='utf-8')
    print('  index.html')


def main() -> None:
    p = argparse.ArgumentParser(description='Render Turmeric guide markdown to HTML.')
    p.add_argument('guides_dir', help='Path to docs/guides/ directory')
    p.add_argument('--out', default=None, help='Output directory (default: same as guides_dir)')
    args = p.parse_args()

    guides_dir = Path(args.guides_dir)
    out_dir = Path(args.out) if args.out else guides_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    md_files = sorted(f for f in guides_dir.glob('*.md') if f.stem != 'README')
    all_stems = {f.stem for f in md_files}

    meta_by_stem: dict = {}
    for src in md_files:
        fm, _ = parse_front_matter(src.read_text(encoding='utf-8'))
        meta_by_stem[src.stem] = fm

    categories = build_categories_from_meta(meta_by_stem, all_stems)

    # Git dates for the Recently Added / Updated tabs. A checkout with no
    # history (or no git) yields None dates and the tabs are left out.
    repo_root = guides_dir.resolve()
    while repo_root != repo_root.parent and not (repo_root / '.git').exists():
        repo_root = repo_root.parent

    def label_for(src: Path) -> str:
        m = meta_by_stem.get(src.stem, {})
        return m.get('title', src.stem.replace('-', ' ').title()).strip()

    dates = {src: _git_dates(src, repo_root) for src in md_files}
    added = sorted(((a, src) for src, (a, _) in dates.items() if a),
                   key=lambda t: t[0], reverse=True)
    recent = [{'stem': src.stem, 'label': label_for(src), 'date': d}
              for d, src in added[:10]]
    edited = sorted(((u, src) for src, (a, u) in dates.items() if u and u != a),
                    key=lambda t: t[0], reverse=True)
    recent_updated = [{'stem': src.stem, 'label': label_for(src), 'date': d}
                      for d, src in edited[:10]]

    print('Generating guides:')
    for src in md_files:
        render_guide(src.stem, src, out_dir / f'{src.stem}.html', all_stems,
                     meta_by_stem.get(src.stem, {}))
    render_index(categories, all_stems, out_dir, recent=recent,
                 recent_updated=recent_updated)
    print(f'Done: {len(md_files)} guides + index → {out_dir}')


if __name__ == '__main__':
    main()
