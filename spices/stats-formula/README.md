# tur-stats-formula

Wilkinson-Rogers model formulas for [tur-frame](../frame/) and
[tur-stats](../stats/): write `mpg ~ wt * hp + I(wt^2) + fuel` and get the
model frame and predictor list `ols-frame` takes, with R's term expansion,
treatment contrasts and `na.omit`.

```turmeric
(import frame/csv :refer [read-csv])
(import frame/typed :refer [frame-typed])
(import stats/regress :refer [ols-frame])
(import formula/terms
  :refer
  [formula-parse-terms
   formula-terms-response
   formula-terms-predictors
   formula-terms-intercept?
   formula-terms-data
   formula-terms-free])
(import formula/model-frame :refer [model-frame-handle])

(match (formula-parse-terms (frame-typed (read-csv "mtcars.csv" 44 34 1 100 ""))
                            "mpg ~ wt * hp + I(wt^2)")
  (Ok t) (let [fit (ols-frame (model-frame-handle (formula-terms-data t))
                              (formula-terms-response t)     ; "mpg"
                              (formula-terms-predictors t)   ; ("wt" "hp" "I(wt^2)" "wt:hp")
                              (if (formula-terms-intercept? t) 1 0))]
           (formula-terms-free t)
           fit)
  (Err e) ...)
```

The guide is [docs/guides/formula-guide.md](../../docs/guides/formula-guide.md);
[examples/01_tour.tur](examples/01_tour.tur) runs everything in it.

## Modules

| Module | Exports |
| --- | --- |
| `formula/terms` | `formula-parse-terms`, `FormulaTerms` and its accessors, `formula-predictors`, `formula-response`, `formula-intercept?`, `formula-term-labels` |
| `formula/parser` | `formula-parse`, `formula-parse-tokens`, `formula-parse-expr`, `formula-function?` |
| `formula/lexer` | `formula-lex`, `Token`, `TokKind` |
| `formula/expand` | `formula-expand`, `Expansion`, `Term` |
| `formula/factors` | `formula-is-factor`, `formula-factor-levels`, `formula-dummy-code`, `formula-dummy-code-interaction` |
| `formula/identity` | `formula-eval-identity` |
| `formula/model-frame` | `ModelFrame`, `model-frame-handle`, `model-frame-free`, `model-frame-names`, `model-frame-value`, ... |
| `formula/ast` | `Expr`, `Formula`, `ParsedFormula`, `FormulaError`, `FormulaErrorKind`, the renderers |
| `formula/seq`, `formula/text` | small `(Cons A)` and byte helpers the others share |

Depends on tur-frame. tur-stats is an optional dependency, used only by the
tests (which fit mtcars and iris and compare with R's `lm()`).

## Tests

```sh
tur test tests/formula
```

## Compiler workarounds

Four turmeric bugs shaped the code; each is filed, and the code says where it
works around one:

- **No `Option` in a struct field.** Any struct with an `Option` field makes
  the C for a `(Cons T)` of a user ADT come before T's type
  ([monomorph-emitted-before-its-type-argument](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/monomorph-emitted-before-its-type-argument.md)).
  `FormulaError` keeps its position as an `ErrPos` ADT and `ParsedFormula`
  its response as an `Lhs` ADT; the accessors return `Option`.
- **`(Cons A)`, not stdlib `(List A)`.** Loading `stdlib/list-typed.tur` in
  a module collides with tur-frame's `String`
  ([list-typed-load-collides-with-frame-ownstr-string](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/list-typed-load-collides-with-frame-ownstr-string.md)),
  so this spice could not be used with `frame/print`.
- **No stdlib `cstr` import.** It breaks `tur build` of the spice
  ([stdlib-cstr-import-breaks-spice-library-build](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/stdlib-cstr-import-breaks-spice-library-build.md));
  `formula/text` has the four byte helpers instead.
- **Hand-written recursion over `defdata` lists.** A capturing lambda with a
  `defdata`-typed parameter is refused by a generic function parameter
  ([closure-with-adt-param-rejected-by-generic-fn-param](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/closure-with-adt-param-rejected-by-generic-fn-param.md)).
  List elements are `^heap` `defdata`s rather than structs, because a
  `(Cons T)` holds a word.
