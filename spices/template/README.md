# tur-template

ERB/EJS-style string templating engine for Turmeric. Interpolate
variables, branch with `if`/`else`, iterate with `for`, and bind locals
with `let` -- all in plain text templates with no embedded logic
language.

## Overview

`tur-template` is a Tier 1 spice (pure Turmeric, no C deps beyond the
inline-C renderer). It parses template strings containing `<%= var %>`
interpolation, `<% if cond %>...<% else %>...<% end %>` conditionals,
`<% for [x list] %>...<% end %>` loops, and `<% let x val %>...<% end %>`
bindings, then renders them against an environment of string bindings.

The engine is designed for server-side HTML generation: `tourist` uses
it for view rendering. Templates are plain text (typically `.html.tur`
files); the renderer returns a heap-allocated `cstr` that the caller
frees.

## Install

```turmeric no-check
:spices {
  "template" {:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "template-v0.2.0"
              :subdir "spices/template"}
}
```

## Quick start

```turmeric
(import template/env    :refer [env-new env-set env-free])
(import template/render :refer [render])

(let [e (env-new)]
  (env-set e "title" "Greetings")
  (env-set e "name"  "Roger")
  (println (render "<h1><%= title %></h1><p>Welcome, <%= name %>.</p>" e))
  (env-free e))
```

```sweet-exp
#lang sweet-exp
import template/env    :refer [env-new env-set env-free]
import template/render :refer [render]

let [e env-new()]
  env-set(e "title" "Greetings")
  env-set(e "name"  "Roger")
  println $ render("<h1><%= title %></h1><p>Welcome, <%= name %>.</p>" e)
  env-free(e)
```

### Rendering from a file

```turmeric
(import template/env    :refer [env-new env-set env-free])
(import template/render :refer [render-file])

(let [e (env-new)]
  (env-set e "title" "Greetings")
  (env-set e "name"  "Roger")
  (let [r (render-file "template.html.tur" e)]
    (if (ok? r)
      (println (ok-val r))
      (println (err-val r))))
  (env-free e))
```

### Template syntax

| Syntax | Meaning |
|--------|---------|
| `<%= var %>` | Interpolate the string value of `var` |
| `<% if cond %>...<% else %>...<% end %>` | Conditional branch (truthy = non-empty string) |
| `<% for [x list] %>...<% end %>` | Iterate over a list binding |
| `<% let x val %>...<% end %>` | Bind `x` to `val` for the body |
| `<%% ... %>` | Literal `<% ... %>` (escaped) |

### Modules

| Module | Exports |
|--------|---------|
| `template/render` | `render`, `render-file`, `render-ast` |
| `template/env` | `env-new`, `env-set`, `env-set-list`, `env-get`, `env-has?`, `env-free` |
| `template/token` | `lex`, token accessors, `tokens-free` |
| `template/parse` | `parse`, node accessors, `nodes-free` |
| `template/tree` | `Tpl` tree constructors and accessors |

### Environment

An `Env` is a stack of key-to-string bindings. `env-set` adds or shadows
a scalar binding; `env-set-list` adds an iterable binding consumed by
`for` loops. The renderer pushes temporary bindings during `for`/`let`
blocks and pops them after the body completes.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/template-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/template>
