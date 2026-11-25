# RTK integer search and validation

Set `pos2-arstrategy` to exactly one of `full`, `wlnl` or `par`. The
relative-positioning engine uses that strategy for every epoch, generates
the two best distinct integer vectors, and evaluates one acceptance test. A
validator rejection or incomplete search leaves the solution float. No other
basis, subset, or constellation is tried after seeing the decision. A candidate
accepted by the integer test must still pass the position residual check.

| pos2-arstrategy | Candidate model before validation |
| --- | --- |
| full | All eligible DDs, ordinary LAMBDA. |
| wlnl (default) | All eligible DDs, joint wide/narrow-lane search. At least one matched dual-frequency DD is required. |
| par | Select by covariance, then use ordinary LAMBDA on the selected rows. |

For a matched L1/L2 target **and** reference, the integer coordinates are
`(N1, W=N1-N2)`. Unpaired DDs remain original integer coordinates. Wide lanes
are searched first, but alternative wide-lane branches remain in the search
until they cannot beat the second-best joint cost. With no matched pair,
`wlnl` skips the integer search; choose `full` or `par` for single-frequency
runs. Direct and wide-lane search on the same rows solve the same
integer least-squares problem on the selected rows. Exactly tied integer
solutions need not be returned in the same order.

PAR uses only the input covariance and eligibility/geometry checks. It orders
DD rows by increasing float variance and keeps the longest prefix meeting the
`pos2-parminps` model success floor (default 0.99). The ordered bootstrap
probability is computed from conditional variances. If the complete set meets
the floor, all DDs can be kept. Otherwise a subset must satisfy
`pos2-parminamb` and the minimum distinct-satellite count. PAR does not use
the observed integer ratio or shrink the subset after rejection. The success
floor is a selection parameter, not a measured failure rate or FFRT tolerance.
PAR retaining every eligible row is still one PAR decision.

Old configuration files are translated on load. Both old switches off means
`full`; only `pos2-wlnl=on` means `wlnl`; only `pos2-par=on` means `par`.
With default options, `pos2-par=off` alone retains `wlnl`. A file explicitly
enabling both without the new selector is rejected. An explicit
`pos2-arstrategy` takes precedence over the old switches regardless of line
order. Conflicting repeated strategy settings and invalid values are errors.
Newly saved configurations contain only the new selector and PAR tuning options.

Both candidates are mapped back to original DDs and scored with the same
symmetric covariance. Ratio and FFRT see those joint costs and the actual
dimension. FFRT always uses the reduced bootstrap failure bound of the original
selected DD covariance, regardless of search basis. An active ML hook retains
precedence as the single validator. Acceptance updates position, covariance,
and flags using only the selected constraints; a rejection applies none.

To compare validation methods, hold the search options and float inputs
constant, then switch `pos2-artest` between `ratio` and `ffrt`. Separate
stateful runs may eventually have different float inputs or satellites as a
result of earlier decisions. The FFRT tolerance is model-based; its observed
failure rate requires data with independently known integers. A known station
position can measure position error.

Trace level 2 emits one `AR decision` per evaluated candidate pair, with its
epoch, configured strategy, actual search basis, selected/available dimensions,
matched pairs, validator, two joint costs and decision. Epochs with no admissible
subset or no matched wide-lane pair log a skip.
