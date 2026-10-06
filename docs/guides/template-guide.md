---
title: String Templating Engine
category: Networking and Web
description: ERB/EJS-style string templating -- interpolation, conditionals, loops, and local bindings for server-side HTML generation
audience: developers rendering HTML, email, or any text output from templates
since: template v0.2.0
---

# tur-template Guide

`tur-template` is an ERB/EJS-style string templating engine. It parses
template strings containing `<%= var %>` interpolation, `<% if cond
%>...<% else %>...<% end %>` conditionals, `<% for [x list] %>...<% end %>`
loops, and `<% let x val %>...<% end %>` bindings, then renders them
against an environment of string bindings.

The engine is designed for server-side HTML generation: `tur-tourist`
uses it for view rendering. Templates are plain text (typically
`.html.tur` files); the renderer returns a heap-allocated `cstr` that the
caller frees.

This guide walks the five things you'll do most often:

1. [Render a template string](#1-rendering-a-template-string)
2. [Render a template file](#2-rendering-a-template-file)
3. [Conditionals and loops](#3-conditionals-and-loops)
4. [Local bindings and escaping](#4-local-bindings-and-escaping)
5. [Using templates with tourist](#5-using-templates-with-tourist)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "template" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "template-v0.2.0"
               :subdir "spices/template"}
}
```

Then `tur fetch`. No CMake dependency -- `tur-template` is pure Turmeric
with an inline-C renderer.

---

## 1. Rendering a template string

```turmeric
(import template/env    :refer [env-new env-set env-free])
(import template/render :refer [render])

(defn greet [name : cstr] : cstr
  (let [e (env-new)]
    (env-set e "name" name)
    (let [out (render "Hello, <%= name %>!" e)]
      (env-free e)
      out)))
```

```sweet-exp
#lang sweet-exp
import template/env :refer [env-new env-set env-free]
import template/render :refer [render]

defn greet [name :cstr] :cstr
  let [e env-new()]
    env-set(e "name" name)
    let [out render("Hello, <%= name %>!" e)]
      env-free(e)
      out
```

`render` returns a heap-allocated `cstr` that the caller must free. The
`env` is a stack of key-to-string bindings; `env-set` adds or shadows a
scalar binding. Free the env with `env-free` when done -- the rendered
string is independent of the env after `render` returns.

---

## 2. Rendering a template file

```turmeric
(import template/env    :refer [env-new env-set env-free])
(import template/render :refer [render-file])

(defn render-page [path : cstr title : cstr] : cstr
  (let [e (env-new)]
    (env-set e "title" title)
    (let [r (render-file path e)]
      (env-free e)
      (if (ok? r)
        (ok-val r)
        "render error"))))
```

```sweet-exp
#lang sweet-exp
import template/env :refer [env-new env-set env-free]
import template/render :refer [render-file]

defn render-page [path :cstr title :cstr] :cstr
  let [e env-new()]
    env-set(e "title" title)
    let [r render-file(path e)]
      env-free(e)
      if ok?(r)
        ok-val(r)
        "render error"
```

`render-file` returns `(Result cstr cstr)` -- `ok` is a heap-allocated
string the caller frees, `err` is a static diagnostic. This is safer than
`render` for file I/O: a missing file returns `err` instead of crashing.

A simple view template (`views/home.html.tur`):

```html
<h1><%= title %></h1>
<p>Welcome to the home page.</p>
```

---

## 3. Conditionals and loops

### Conditionals

`<% if cond %>...<% else %>...<% end %>` branches on the truthiness of an
env binding. A value is truthy when it is a non-empty string.

Template (`views/greeting.html.tur`):

```html
<% if logged-in %>Welcome back, <%= name %>!<% else %>Please <a href="/login">sign in</a>.<% end %>
```

Render it:

```turmeric
(let [e (env-new)]
  (env-set e "logged-in" "true")
  (env-set e "name" "Alice")
  (let [r (render-file "views/greeting.html.tur" e)]
    (env-free e)
    (if (ok? r) (println (ok-val r)))))
```

### Loops

`<% for [x list] %>...<% end %>` iterates over a list binding. The loop
variable `x` is bound to each element for the body.

Template (`views/list.html.tur`):

```html
<ul>
<% for [item items] %>  <li><%= item %></li>
<% end %></ul>
```

Render it with a list:

```turmeric
(import template/env :refer [env-new env-set env-set-list env-free])

(let [e (env-new)]
  (env-set-list e "items" (cons3 "alpha" "beta" "gamma"))
  (let [r (render-file "views/list.html.tur" e)]
    (env-free e)
    (if (ok? r) (println (ok-val r)))))
```

`env-set-list` binds an iterable binding consumed by `for` blocks. The list
is a cons-list of cstr values (int handles pointing to string cells).

---

## 4. Local bindings and escaping

### Local bindings

`<% let x val %>...<% end %>` binds `x` to `val` for the body. If `val`
starts with `@`, it is resolved as an env lookup of the key after `@`;
otherwise it is a literal string (with surrounding `""` stripped if any).

Template:

```html
<% let greeting "Hello" %><%= greeting %>, <%= name %>!<% end %>
```

This renders as `Hello, Alice!` when `name` is `Alice`.

### Escaping

`<%%` emits a literal `<%` in the output. This is for templates that need
to produce `<%` text without it being interpreted as a tag:

Template:

```html
Literal tag: <%% if x %>, value: <%= val %>.
```

With `x` unset and `val` set to `"hello"`, this renders as:

```
Literal tag: <% if x %>, value: hello.
```

### Comments

`<%# ... %>` is a comment -- the content is discarded by the parser and
produces no output:

```html
<%# This is a comment and will not appear in the output %>
<p>Visible text.</p>
```

---

## 5. Using templates with tourist

The most common use case: render a template inside a tourist route handler
and return it as an HTML response.

```turmeric
(import tourist/app      :refer [tourist])
(import tourist/dsl      :refer [get!])
(import tourist/helpers  :refer [html])
(import tourist/param    :refer [capture])
(import template/render  :refer [render-file])
(import template/env     :refer [env-new env-set env-free])

(defn render-view [path : cstr env : int] : cstr
  (let [r (render-file path env)]
    (env-free env)
    (if (ok? r)
      (ok-val r)
      "render error")))

(defn main [] : int
  (let [s (tourist 3000
            (get! "/hello/:name"
              (fn [ctx]
                (let [e (env-new)
                      n (ok-val (capture ctx "name"))]
                  (env-set e "name" n)
                  (html (render-view "views/hello.html.tur" e)))))]
    (server-stop s)
    0))
```

```sweet-exp
#lang sweet-exp
import tourist/app :refer [tourist]
import tourist/dsl :refer [get!]
import tourist/helpers :refer [html]
import tourist/param :refer [capture]
import template/render :refer [render-file]
import template/env :refer [env-new env-set env-free]

defn render-view [path :cstr env :int] :cstr
  let [r render-file(path env)]
    env-free(env)
    if ok?(r)
      ok-val(r)
      "render error"

defn main [] :int
  let [s tourist(3000
            get!("/hello/:name"
              (fn [ctx]
                let [e env-new()
                     n ok-val(capture(ctx "name"))]
                  env-set(e "name" n)
                  html(render-view("views/hello.html.tur" e)))))]
    server-stop(s)
    0
```

`render-view` returns `render-file`'s heap buffer past `env-free`, so the
caller owns those bytes. The `html` helper wraps it in a 200
`text/html` response.

For the full web stack -- routing, sessions, TLS, and templates together
-- see the [tourist web stack end-to-end guide](https://turmeric-lang.com/docs/html/guides/tourist-web-stack-guide.html).

---

## Template syntax reference

| Syntax | Meaning |
|--------|---------|
| `<%= var %>` | Interpolate the string value of `var` |
| `<% if cond %>...<% else %>...<% end %>` | Conditional branch (truthy = non-empty string) |
| `<% for [x list] %>...<% end %>` | Iterate over a list binding |
| `<% let x val %>...<% end %>` | Bind `x` to `val` for the body (`@key` = env lookup) |
| `<%# comment %>` | Comment (discarded, not rendered) |
| `<%%` | Literal `<%` in output |

The DSL inside `<% ... %>` is parsed structurally; nothing is evaluated as
Turmeric code. Unknown forms are silently treated as no-ops.

---

## Module reference

| Module | Exports |
|--------|---------|
| `template/render` | `render`, `render-file`, `render-ast` |
| `template/env` | `env-new`, `env-set`, `env-set-list`, `env-get`, `env-has?`, `env-free` |
| `template/token` | `lex`, `token-tag`, `token-text`, `token-len`, `token-next`, `tokens-free` |
| `template/parse` | `parse`, `node-tag`, `node-name`, `node-value`, `node-then`, `node-else`, `node-next`, `nodes-free` |
| `template/tree` | `Tpl` tree constructors and accessors |

### Low-level pipeline

For cases where you want to cache the parsed AST and render it multiple
times with different environments:

```turmeric
(import template/token  :refer [lex tokens-free])
(import template/parse  :refer [parse nodes-free])
(import template/render :refer [render-ast])

(let [src "Hello, <%= name %>!"
      toks (lex src)
      ast  (parse toks)]
  ;; Render multiple times with different envs
  (let [e1 (env-new)]
    (env-set e1 "name" "Alice")
    (println (render-ast ast e1))
    (env-free e1))
  (let [e2 (env-new)]
    (env-set e2 "name" "Bob")
    (println (render-ast ast e2))
    (env-free e2))
  (nodes-free ast)
  (tokens-free toks))
```

---

## Environment API

An `Env` is a stack of key-to-string bindings. The stack layout lets the
renderer push temporary bindings (during `for`/`let` blocks) and pop them
after the body completes.

```turmeric
(env-new)                           ;; create an empty Env handle
(env-set  env "key" "value")        ;; bind a scalar string
(env-set-list env "items" lst)      ;; bind a cons list of cstr values
(env-get  env "key")                ;; retrieve a value (cstr, or 0)
(env-has? env "key")                 ;; 1 if key exists, 0 otherwise
(env-free env)                       ;; release the Env
```

`env-set` adds or shadows a binding. `env-set-list` adds an iterable
binding consumed by `for` blocks. Both copy the key and value into the
env's own storage, so the caller's strings are safe to free afterward.

---

## See also

- [API reference](api/)
- [README](../../spices/template/README.md) -- module reference and install
- [Web Stack Guide](https://turmeric-lang.com/docs/html/guides/web-stack-guide.html) -- httpd + template + tourist
- [Tourist Web Stack End-to-End](https://turmeric-lang.com/docs/html/guides/tourist-web-stack-guide.html) -- full application with TLS, sessions, and templates
