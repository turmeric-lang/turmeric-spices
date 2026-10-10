# Math Walkthrough

This notebook demonstrates inline math, display math, multi-line equations,
a `math=true` output cell, and a deliberately broken expression.

---

## Inline math

The quadratic formula gives the roots of $ax^2 + bx + c = 0$ as

$$
x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}
$$

In probability, the expected value is $\mathbb{E}[X] = \sum_x x \, P(X = x)$
for a discrete random variable.

---

## Display math

The Gaussian (normal) probability density function:

$$
f(x) = \frac{1}{\sigma\sqrt{2\pi}}\,
       e^{-\tfrac{1}{2}\left(\tfrac{x - \mu}{\sigma}\right)^2}
$$

When $\sigma = 1$ and $\mu = 0$ this reduces to the **standard normal**
$\mathcal{N}(0, 1)$.

---

## Multi-line equations

Maxwell's equations in differential form:

$$
\nabla \cdot \mathbf{E} = \frac{\rho}{\varepsilon_0}
$$

$$
\nabla \cdot \mathbf{B} = 0
$$

$$
\nabla \times \mathbf{E} = -\frac{\partial \mathbf{B}}{\partial t}
$$

$$
\nabla \times \mathbf{B} = \mu_0\mathbf{J}
  + \mu_0\varepsilon_0\frac{\partial \mathbf{E}}{\partial t}
$$

---

## Computing with math

The cell below computes the golden ratio $\varphi = \frac{1 + \sqrt{5}}{2}$:

```turmeric
(load "stdlib/math.tur")

(defn golden-ratio [] :float
  (* 0.5 (+ 1.0 (sqrt 5.0))))

(println (golden-ratio))
```

---

## Runtime math output (`math=true`)

A cell with `math=true` renders its stdout as KaTeX math rather than
preformatted text.  The `$$...$$` in the printed string becomes a live
math expression:

```turmeric {math=true}
(println "$$ \\varphi = \\frac{1 + \\sqrt{5}}{2} \\approx 1.618 $$")
```

---

## Broken expression (error color)

KaTeX renders unsupported or malformed expressions in red and continues
rendering the rest of the page.  The expression below uses `\bogus`, which
does not exist in KaTeX:

$$
\bogus{x} = \frac{1}{2}
$$

The text before and after this block is unaffected.

---

## Currency dollar signs

Dollar signs followed by a digit or whitespace are **not** parsed as math,
so prices like $5, $10, and $99.99 render as plain text.
