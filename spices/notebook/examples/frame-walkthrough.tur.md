# Data Frame Walkthrough

This notebook demonstrates the `tur-frame` spice: loading CSV data,
inspecting shape, filtering, group-by aggregation, and printing.

---

## Loading a CSV from a string

`read-csv-string` takes a CSV string, delimiter (0 = comma), quote
character (0 = double-quote), has-header flag (1 = yes), infer-rows
(0 = scan 100 rows), and a null string. It returns a frame handle.

```turmeric
(import frame/csv   :refer [read-csv-string])
(import frame/frame  :refer [frame-nrows frame-ncols frame-head])
(import frame/print  :refer [print-frame])

(def csv "name,age,city\nAlice,30,NYC\nBob,25,SF\nCarol,35,NYC\nDave,28,LA\n")
(def df (read-csv-string csv 0 0 1 0 ""))

(println (frame-nrows df))   ;; rows
(println (frame-ncols df))   ;; columns
(print-frame (frame-head df 3))
```

---

## Filtering rows

`filter-mask` takes a frame and a boolean column mask. Build the mask
with `column-bool` from a list of 0/1 values.

```turmeric
(import frame/csv     :refer [read-csv-string])
(import frame/filter  :refer [filter-mask])
(import frame/column  :refer [column-bool])
(import frame/print   :refer [print-frame])

(def csv "name,age,city\nAlice,30,NYC\nBob,25,SF\nCarol,35,NYC\nDave,28,LA\n")
(def df (read-csv-string csv 0 0 1 0 ""))

;; Keep rows where age > 26 (Alice, Carol, Dave)
(def mask (column-bool (cons 1 (cons 0 (cons 1 (cons 1 0)))) 0 0))
(print-frame (filter-mask df mask))
```

---

## Group-by and aggregation

`group-by` takes a frame and a vector of key column names. `agg` takes
the grouped object, three parallel lists (output names, input names,
aggregation tags), and returns a result frame. frame's handles are `:int`,
so a column name goes in as `(:: "name" :int)`.

```turmeric
(import frame/csv   :refer [read-csv-string])
(import frame/group :refer [group-by grouped-free agg
                             agg-count agg-mean])
(import frame/print :refer [print-frame])

(def csv "city,age\nNYC,30\nSF,25\nNYC,35\nLA,28\nNYC,22\nSF,40\n")
(def df (read-csv-string csv 0 0 1 0 ""))

(def g (group-by df (vec-of (:: "city" :int))))

;; Two aggregations: count and mean age, per city
(def outs (cons (:: "n"   :int)
          (cons (:: "avg" :int) 0)))
(def ins  (cons (:: "age" :int)
          (cons (:: "age" :int) 0)))
(def tags (cons (agg-count)
          (cons (agg-mean) 0)))

(print-frame (agg g outs ins tags))
(grouped-free g)
```

---

## Using read-csv (file-based)

For production, use `read-csv` with a file path. The signature is the
same as `read-csv-string` but the first argument is a file path instead
of a string.

```turmeric
(import frame/csv  :refer [read-csv])
(import frame/print :refer [print-frame])

;; (def df (read-csv "data.csv" 0 0 1 0 ""))
;; (print-frame (frame-head df 10))
```

The cell above is commented out because there is no `data.csv` in this
example. In a real notebook, replace the path with your CSV file.
