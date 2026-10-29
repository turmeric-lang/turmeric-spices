// check.js -- load fixtures/browser/pages.tur's pages in headless Chromium
// (xml-html-followups-plan A6's browser check).
//
// For each page: click the three onclick buttons, then check that __pwn()
// never ran, that every JavaScript and JSON string hole reads back as the
// value (pN.txt), that the <style> content string does too, and that the
// srcdoc iframe shows the value as text. Prints TAP; exits 1 on a failure.
//
// Usage: node check.js <dir>
'use strict';
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

// A CSS string as getComputedStyle serializes it, back to its value.
function cssUnquote(s) {
  if (!/^".*"$/s.test(s)) return null;
  s = s.slice(1, -1);
  let out = '';
  for (let i = 0; i < s.length; i++) {
    if (s[i] !== '\\') { out += s[i]; continue; }
    const m = /^[0-9a-fA-F]{1,6}[ \t\n]?/.exec(s.slice(i + 1));
    if (m) {
      out += String.fromCodePoint(parseInt(m[0].trim(), 16));
      i += m[0].length;
    } else {
      out += s[i + 1];
      i++;
    }
  }
  return out;
}

(async () => {
  const dir = process.argv[2];
  const pages = fs.readdirSync(dir).filter((f) => /^p\d+\.html$/.test(f))
    .sort((a, b) => parseInt(a.slice(1)) - parseInt(b.slice(1)));
  const browser = await chromium.launch();
  let fails = 0, n = 0;
  console.log(`1..${pages.length}`);
  for (const p of pages) {
    n++;
    const want = fs.readFileSync(path.join(dir, p.replace('.html', '.txt')), 'utf8');
    const page = await browser.newPage();
    const errs = [];
    page.on('dialog', (d) => d.dismiss());
    await page.goto('file://' + path.resolve(dir, p));
    for (const id of ['#b1', '#b2', '#b3']) await page.click(id);
    await page.waitForFunction(() => {
      const f = document.getElementById('f');
      return f.contentDocument && f.contentDocument.readyState === 'complete';
    });
    await page.waitForTimeout(50);
    const got = await page.evaluate(() => {
      const j = JSON.parse(document.getElementById('j').textContent);
      return {
        pwned: window.__pwned,
        r: [window.__r1, window.__r2, window.__r3, window.__r4, window.__r5, window.__r6, j.a, j.b],
        content: getComputedStyle(document.getElementById('s2'), '::after').content,
        frame: document.getElementById('f').contentDocument.body
          ? document.getElementById('f').contentDocument.body.textContent : null,
        frameElements: document.getElementById('f').contentDocument.body
          ? document.getElementById('f').contentDocument.body.children.length : -1,
      };
    }).catch((e) => ({ error: String(e) }));
    if (got.error) errs.push(got.error);
    else {
      if (got.pwned !== false) errs.push('__pwn() ran');
      got.r.forEach((v, i) => { if (v !== want) errs.push(`string hole ${i + 1} read back ${JSON.stringify(v)}`); });
      if (cssUnquote(got.content) !== want) errs.push(`CSS content read back ${got.content}`);
      // The HTML parser drops leading whitespace before the body, as it would for any text.
      if ((got.frame || '').replace(/^[\t\n\f\r ]+/, '') !== want.replace(/^[\t\n\f\r ]+/, ''))
        errs.push(`srcdoc text read back ${JSON.stringify(got.frame)}`);
      if (got.frameElements !== 0) errs.push('srcdoc text made elements');
    }
    await page.close();
    if (errs.length) {
      fails++;
      console.log(`not ok ${n} - ${p} ${JSON.stringify(want)}`);
      errs.forEach((e) => console.log(`#   ${e}`));
    } else {
      console.log(`ok ${n} - ${p} ${JSON.stringify(want)}`);
    }
  }
  await browser.close();
  console.log(fails ? `# ${fails} of ${n} pages failed` : `# all ${n} pages passed`);
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
