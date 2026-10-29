---
title: Model Formulas
category: Data
description: Wilkinson-Rogers model formulas for tur-frame and tur-stats -- write `mpg ~ wt * hp + I(wt^2)` and get the model frame and predictor list `ols-frame` takes, with R's term expansion and factor coding
audience: developers fitting models with tur-stats who would rather write the model than build its design matrix by hand
since: stats-formula v0.1.0
---

# tur-stats-formula Guide

`ols-frame` takes a frame, a response column, a list of predictor columns and
an intercept flag. That is enough for `mpg ~ wt + hp`, and a chore for
anything more: an interaction is a product column you compute yourself, a
squared term is another, and a categorical predictor is one indicator column
per level but one. A formula says all of that in one line:

```
mpg ~ wt * hp + I(wt^2) + fuel
```

`tur-stats-formula` reads that line the way R does and hands back exactly
what `ols-frame` wants: a model frame holding the response and every
model-matrix column, and the list of predictor names in it. No change to
`tur-stats` is needed.

1. [Formula syntax](#1-formula-syntax)
2. [Simple models](#2-simple-models)
3. [Interactions: `*` and `:`](#3-interactions)
4. [Factors and dummy coding](#4-factors-and-dummy-coding)
5. [Computed variables: `I()` and functions](#5-computed-variables)
6. [Intercept control](#6-intercept-control)
7. [Fitting with tur-stats](#7-fitting-with-tur-stats)
8. [Comparison with R](#8-comparison-with-r)

Everything below is from one runnable program,
[`spices/stats-formula/examples/01_tour.tur`](../../spices/stats-formula/examples/01_tour.tur)
(`tur run examples/01_tour.tur` in `spices/stats-formula`), over ten cars:

```
mpg,wt,hp,fuel
21.0,2.620,110,petrol
22.8,2.320,93,petrol
...
17.8,,123,petrol        <- no weight
```

## 0. Installing the spice

```turmeric no-check
:spices #map{
  "frame"         #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                       :ref    "frame-v0.1.0"
                       :subdir "spices/frame"}
  "stats-formula" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                       :ref    "stats-formula-v0.1.0"
                       :subdir "spices/stats-formula"}}
```

Add `tur-stats` too to fit models. The modules:

| Module | What it is |
| --- | --- |
| `formula/terms` | The entry point: `formula-parse-terms` and the `FormulaTerms` it returns |
| `formula/parser`, `formula/lexer` | Formula text to a `ParsedFormula` |
| `formula/expand` | A `ParsedFormula` to its terms (`a * b` to `a`, `b`, `a:b`) |
| `formula/factors` | Factor levels and dummy coding |
| `formula/identity` | Evaluating `I(...)` and function calls over a frame |
| `formula/model-frame` | `ModelFrame`, the frames this spice builds |
| `formula/ast` | The trees, and `FormulaError` |

## 1. Formula syntax

A formula is a response, `~`, and a right-hand side of terms:

| Write | Means |
| --- | --- |
| `y ~ a + b` | `a` and `b` |
| `y ~ a:b` | the interaction of `a` and `b` alone |
| `y ~ a * b` | `a + b + a:b` |
| `y ~ a / b` | `a + a:b` (`b` nested in `a`) |
| `y ~ (a + b + c)^2` | every term up to two-way interactions |
| `y ~ a * b - a:b` | remove a term |
| `y ~ x - 1`, `y ~ 0 + x` | no intercept |
| `y ~ I(x^2)`, `y ~ log(x)` | a computed variable |
| `` `fuel economy` ~ x `` | a column name that is not one token |
| `~ a + b` | one-sided: expands, but `formula-parse-terms` needs a response |

Operators bind, tightest first: `^`, then `:`, then `*` and `/`, then unary
`-`, then `+` and `-`. Names follow R -- letters, digits, `.` and `_`, not
starting with a digit -- and UTF-8 is fine.

`formula-term-labels` shows what a formula expands to, with no data:

```
mpg ~ wt * hp              =>   wt hp wt:hp
mpg ~ (wt + hp + fuel)^2   =>   wt hp fuel wt:hp wt:fuel hp:fuel
mpg ~ fuel / wt            =>   fuel fuel:wt
mpg ~ wt + I(wt^2) - 1     =>   wt I(wt^2)
```

Terms come out main effects first, then two-way interactions and so on, each
group in the order written, exactly as R's `terms()` orders them.

## 2. Simple models

```turmeric no-check
(match (formula-parse-terms (frame-typed (cars)) "mpg ~ wt + hp")
  (Ok t) (do (formula-terms-response t)     ; => "mpg"
             (formula-terms-predictors t)   ; => ("wt" "hp")
             (formula-terms-intercept? t)   ; => true
             (formula-terms-free t))
  (Err e) (println (formula-error->string e)))
```

`formula-parse-terms` takes a row-typed `(Frame cols)` (wrap a raw frame
with `frame-typed`) and returns `(Result FormulaTerms FormulaError)`. The
`FormulaTerms` owns a new **model frame** -- free it with
`formula-terms-free`. Its columns are the response, then one float64 column
per predictor, over the rows where every variable the formula uses is
present:

```
log(mpg) ~ fuel + wt:hp

| log(mpg) | fuelhybrid | fuelpetrol | wt:hp   |
|----------|------------|------------|---------|
| 3.04452  | 0          | 1          | 288.2   |
| 3.12676  | 0          | 1          | 215.76  |
...                                      (9 rows: the car with no weight is out)
```

`formula-terms-dropped` says how many rows were left out -- 1 here. Only the
variables the formula uses count: a null in a column it does not mention
drops nothing.

## 3. Interactions

`a:b` is the interaction alone; `a * b` is `a + b + a:b`. For two numeric
columns the interaction is their product, named `wt:hp`. A variable crossed
with itself is itself, so `x^2` in a formula is just `x` -- as in R. To
square a column, say so: `I(x^2)` (section 5).

`(a + b + c)^2` crosses the sum with itself and stops at two-way
interactions; `^3` would add `a:b:c`. `/` nests: `a / b` is `a + a:b`, and
`(a + b) / c` is `a + b + a:b:c` -- the nested term interacts with every
variable on the left.

## 4. Factors and dummy coding

A **factor** is a utf8 column. Every other column type is numeric -- int,
float, date, and bool (as 0 / 1). A column of numeric codes (`cyl` = 4, 6, 8)
stays numeric, as it does in R without `factor(cyl)`; cast it to utf8 with
tur-frame's `column-cast` to make it a factor.

A factor's **levels** are its distinct values in the model's rows, sorted by
byte order: `diesel`, `hybrid`, `petrol`. It enters a model through
**treatment contrasts** -- one indicator column per level except the first,
named column + level:

```
mpg ~ wt + fuel    =>   wt fuelhybrid fuelpetrol
```

`diesel` is the baseline the intercept carries. Which levels a factor gets in
each term follows R's `model.matrix`:

| Formula | Columns |
| --- | --- |
| `y ~ g` | `gb gc` |
| `y ~ g - 1` | `ga gb gc` |
| `y ~ g + h - 1` | `ga gb gc hq` |
| `y ~ g * h` | `gb gc hq gb:hq gc:hq` |
| `y ~ g:x` | `ga:x gb:x gc:x` |
| `y ~ x + g:x` | `x x:gb x:gc` |
| `y ~ g / x` | `gb gc ga:x gb:x gc:x` |
| `y ~ g:h` | `ga:hp gb:hp gc:hp ga:hq gb:hq gc:hq` |

The rule: a factor in a term gets contrasts when the term without it is
empty or appears earlier in the model, and every level otherwise; with no
intercept, the first factor of the first term that has one gets every level.
In an interaction the first variable varies fastest.

The pieces are available on their own: `formula-is-factor`,
`formula-factor-levels`, `formula-dummy-code` (contrasts, with an optional
base level: `(formula-dummy-code f "fuel" (Some "petrol"))`) and
`formula-dummy-code-interaction` (every level of each factor). Those keep
every row of the frame, null where the factor is null.

## 5. Computed variables

`I(...)` evaluates arithmetic over a row: `+ - * / ^`, unary minus,
numbers, columns, and the functions `log` (natural), `exp`, `sqrt` and
`abs`. The functions also work bare -- `log(hp)` is a variable labelled
`log(hp)` -- and so does the response: `log(mpg) ~ ...`. Inside `I()` and a
function's argument, `^` and `*` are arithmetic, not formula operators.

Each computed variable becomes a column named by its label, spelled as R
deparses it: `I(wt^2)`, `I(-x^2/(y - 1))`, `log(hp)`. A row where an input is
null is dropped with the rest of the model's incomplete rows. A row where the
inputs are present but the result is not finite is an error naming it:

```
'log(x)' is not finite at row 2
```

R would carry a NaN there and `na.omit` would drop the row silently; here a
data problem is reported rather than hidden. `formula-eval-identity f
"I(wt^2)"` evaluates one expression into a one-column `ModelFrame`.

## 6. Intercept control

A model has an intercept unless the formula removes it. `- 1`, `+ 0` and a
leading `-1 +` remove it; `+ 1` (or `- 0`) puts it back, and the last word
wins: `y ~ 0 + x + 1` has one. `formula-intercept?` answers without a frame;
`formula-terms-intercept?` is what to pass on to `ols-frame`.

## 7. Fitting with tur-stats

```turmeric no-check
(match (formula-parse-terms (frame-typed (cars)) "mpg ~ wt + fuel")
  (Ok t) (do (print-fit (fit-of (ols-frame (model-frame-handle (formula-terms-data t))
                                           (formula-terms-response t)
                                           (formula-terms-predictors t)
                                           (if (formula-terms-intercept? t) 1 0))))
             (formula-terms-free t))
  (Err e) (println (formula-error->string e)))
```

```
Name               Estimate    Std.Err  t value   Pr(>|t|)
(Intercept)       41.738710   6.087242   6.8568   0.001008 **
wt                -6.215523   1.817542  -3.4197    0.01877 *
fuelhybrid        -0.258563   1.826076  -0.1416     0.8929
fuelpetrol        -3.969836   1.622667  -2.4465    0.05743 .
```

`model-frame-handle` unwraps the model frame to the raw tur-frame handle the
untyped `frame/*` and `stats/*` functions take. `ols-frame` returns an untyped
result cons; `fit-of` in the example takes the fit out of it.

The spice's tests fit mtcars and iris this way and check every coefficient
against R's `lm()` -- `mpg ~ wt * hp`, `mpg ~ wt + I(wt^2)`,
`log(mpg) ~ log(hp) + am`, `mpg ~ (wt + hp + qsec)^2`,
`Sepal.Length ~ Petal.Length + Species`, `Species - 1`, `Species * Petal.Length`
-- to 1e-6. (tur-stats' printed p-values are slightly off R's, and can be
wrong outright; see
[stats-ols-pvalues-wrong-continued-fraction](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/stats-ols-pvalues-wrong-continued-fraction.md).
The coefficients and standard errors are right.)

## 8. Comparison with R

The same as R: the operators and their precedence, term expansion and order
(`x:x` is `x`, so `x^2` is `x`), dropping a term that contains the response,
treatment contrasts and when a factor gets every level, column names
(`gb`, `I(x^2)`, `ga:x`), and `na.omit`.

Different, or not there yet:

| R | Here |
| --- | --- |
| `factor(x)`, `as.factor` | cast the column to utf8 |
| levels in locale collation | byte order (capitals before lower case) |
| logical `x` becomes `xTRUE` | bool is numeric 0 / 1, column `x` |
| NaN / Inf in a computed variable is dropped with a warning | an error naming the row |
| `y ~ .` | not yet: list the columns |
| `offset()`, `poly()`, `ns()`, `strata()`, `weights`, `subset` | not yet |
| `log(x, base)`, `log10`, other functions | `I`, `log`, `exp`, `sqrt`, `abs` |
| `cbind(a, b) ~ x` | one response |

## Errors

Every stage returns `(Result _ FormulaError)`. `formula-error-kind` is one of
`SyntaxError`, `UnknownColumn`, `NotNumeric`, `DomainError`,
`NameCollision`, `NoResponse`, `NotCategorical`, `UnknownLevel`,
`TooFewLevels`; `formula-error-pos` is the byte position for a syntax error;
`formula-error->string` renders it:

```
expected a term, found token '*' at position 10      (mpg ~ wt +* hp)
column 'colour' not found in frame
two model columns would both be named 'gb'           (a factor g with level b, and a column gb)
factor 'one' has fewer than two levels in the complete rows
```

## Memory

The parsed trees are `^heap`: each formula allocates a few dozen small nodes
nothing frees, so a program that parses formulas in a loop puts the loop body
in `with-region`. The model frame is the large allocation, and its owner frees
it: `formula-terms-free`, or `model-frame-free` for a `ModelFrame` from
`formula-dummy-code` and friends.

## Tests

`spices/stats-formula/tests/formula/`, one file per module, ~150 cases:
lexing and parse errors with positions, expansion against R's
`terms()`, coding against R's `model.matrix()`, row dropping, every error
kind, and the R fits above.

## See also

- [stats-formula plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/stats-formula-plan.md)
- [tur-frame guide](frame-guide.md)
