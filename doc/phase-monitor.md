# Receiver phase arc monitor

For relative carrier-phase RTK (including a fixed base), set the following in
the processing configuration and rebuild applications and the library together:

    pos2-slipmon = shadow
    pos2-slipgap = 120

`legacy` (default) uses the existing detector. `shadow` tags observations and
writes diagnostic events but feeds the legacy decisions to the solver.
`apply` uses the tags for ambiguity resets and phase exclusions. PPP, DGPS and
single-point positioning retain their existing behavior. With tracing enabled,
use `rnx2rtkp -x 2` for receiver events or `-x 3` for solver consumption.

* The receiver, satellite, observation code and actual carrier wavelength
  identify the signal in each input slot. A different signal in a slot starts
  a new arc. RINEX post-processing tags the raw receiver epochs before time
  thinning and base pairing; reverse passes use the same tags. The real-time
  server tags at decode. Direct `rtkpos()` callers are monitored on the
  receiver epochs they provide. Thinned post-processing consequently needs
  memory for the raw observations within the requested time span.
* Every available distinct-frequency pair contributes geometry-free phase (m)
  and Melbourne-Wubbena (wide-lane cycles, requires code). GF predicts from the
  median recent clean slope. MW compares with the median clean recent level.
  The nine-sample windows use a 4.5 robust-sigma gate, with floors of
  `pos2-slipthres` metres for GF and 0.75 wide-lane cycles for MW. Setting
  `pos2-slipthres=0` disables GF. A quiet MW cannot veto a GF innovation.
* `pos2-dopthres` is the Doppler rate floor in m/s, multiplied by elapsed time.
  Zero (the legacy default) disables this evidence. When calibrated for the
  receiver and sampling rate, enable it to localize steps on individual bands.
  Trapezoidal phase predictions subtract the receiver's median clock increment
  from at least three **other** satellites. Repeatedly sampled frequencies do
  not count as extra satellites. Missing Doppler is not a vote for continuity.
* A first numerical innovation isolates the sample. Compare subsequent samples
  with the frozen clean arc: a returning phase resumes the old arc; a persistent
  offset starts a new one. Three unresolved observed samples break continuity
  conservatively. A loss-of-lock indication, half-cycle transition, signal
  switch or gap exceeding `pos2-slipgap` seconds starts a new arc without this
  delay. The gap and `unverified` reasons are **not** confirmed physical slips.
* A pair-only event may implicate both signals without localizing either one.
  Trace `reason=pair-step` records that limitation. Residuals/gates are in
  metres for Doppler (key 1) and GF (even keys from 2), or wide-lane cycles for
  MW (odd keys from 3). One gap or one pair event can produce multiple log rows.
  Poor code, strong ionospheric variation, coarse sampling or insufficient
  independent signals limit what can be detected. No integer repair is attempted.
* The solver consumes a boundary once per affected ambiguity. An isolated phase
  cannot initialize a bias, enter a phase residual, or participate in fixing or
  holding; code observations retain their existing checks. A valid new arc can
  enter the float solution immediately, but not fixing in that same epoch, even
  with zero minimum lock. IFLC checks both constituent signals.

The CTest cases `phase_arc_monitor` and `phase_arc_replay` cover raw signal
injections and paired RINEX solves. Before adopting `apply` for a particular
receiver, compare `legacy` and `shadow` on the same sessions and measure
misses and latency on labelled events, false breaks per clean satellite hour,
and incorrect fixes against independent position truth.
