# Selectable MUL clock estimation

One implementation is shared by the DW1000 follower and DWM3000 receiver. The
reference defines the domain and does not call the estimation update. Production
methods are MEDIAN_5 (selector0) and KALMAN_3_STATE (selector2). Selector1 is retired;
it is never interpreted as Kalman. No clock regression remains in production.

Median-5 retains the existing integer interval-rate estimator: validated native40
TX/RX differences, nearest signed-ppb rounding, five most recent valid intervals,
and at least three within1000ppb of their median. Five intervals are required.
Duplicate/reordered observations do not refresh age. Gaps>=3000ms, implausible
sequence/timestamp intervals or rates outside±200000ppb reset the mapping epoch.
The existing arrival-aligned mapping expires after2000ms. No delay is inferred.

Kalman uses double precision on integer-unwrapped local-minus-reference phase.
No absolute timestamp becomes a float. State units are DTU, ppb, ppb/s. For actual
reference elapsed time d seconds and k=63.8976 DTU/s/ppb:

```
F = [[1,k*d,k*d^2/2], [0,1,d], [0,0,1]]
Q = q * [[k^2*d^5/20,k*d^4/8,k*d^3/6],
         [k*d^4/8,d^3/3,d^2/2], [k*d^3/6,d^2/2,d]]
H = [1,0,0]; R=1024 DTU^2; q=1 ppb^2/s^3
```

Initialization is x=0, P=diag(R,1e10,1e4), matching the experiment prototype.
Innovation weighting is min(1,4*sqrt(H P H'+R)/abs(innovation)), with effective
R/weight^2 and Joseph covariance update. Symmetry is restored by averaging the
off-diagonals. Finite checks and a three-state LDL covariance test reject corrupt state.
Nonpositive leading LDL pivots fail closed; final-pivot negative roundoff up to
1e-12 times its covariance diagonal is tolerated; this is a numerical
PSD tolerance, not permission for negative rate variance. No filter tuning uses
the held-out segment. Missing observations advance F/Q over the measured next
interval; public prediction supports up to the existing2s mapping horizon. It
never refreshes the hardware observation age or fabricates a missing reference.

Applied ppb is rounded endpoint frequency plus drift times half the configured
base beacon spacing. This is the reference prototype's average rate over the next
10ms slot in the current setup. Raw mapping origins remain the actual TX/RX pair;
phase is a latent filtering state, not a new synchronized timestamp intercept.
Optional supplied propagation compensation uses the existing delay-validity API
and reinitializes the filter when correction availability changes.

Kalman application requires five accepted intervals, a consistent nonzero mapping
epoch, finite/PSD covariance, abs(rate)<=200000ppb and conditional rate sigma<=1000ppb.
The sigma ceiling deliberately reuses the preexisting Median-5 one-ppm stability
scale; it is a permissive oscillator-trust ceiling, not a physical accuracy bound.
Its consequence is bounded startup confidence and rejection of a corrupt/unbounded
filter, not a claim of millimetre accuracy. After startup, a phase innovation
larger than the existing one-ppm interval gate marks the current raw mapping
origin unavailable even if the robust frequency state remains finite. This
protects against a timestamp spike being reused as a trusted affine origin;
it does not change R/q, Huber weighting or the latent Kalman update. If KF is unreliable, the independently
maintained median applies only when its existing validity gate succeeds; otherwise
mapping remains unavailable. Numerical failure reseeds KF and exposes reason3.
Reasons:0=used,1=warmup,2=uncertain,3=numerical. Huber downweighting increments rejected
count but need not disable an otherwise reliable KF. Both methods retain the
existing2s stale gate; a mathematical prediction does not authorize stale RF use.

The12-byte configuration codec is now version2. Bytes1/2/3 are selector/5/5;
period, historical gate field and compensation flag retain their positions. The
gate field is retained for EEPROM compatibility and does not tune KF. Version1
median/regression settings migrate to Median-5; unknown selectors also become
Median-5. Structural corruption remains an error handled by platform persistence
safety; it must not erase addresses, plans or calibration. Obsolete regression
window controls are retired. Configuration writes are platform-owned and require
RF OFF, as before.

Validation: portable sanitizer tests cover both methods, signs, drift/nonlinearity,
variable/missing intervals, native40/uptime32 wrap, outliers, steps, covariance
failure, fallback/unavailable and outage/reacquisition. The read-only
`tools/test/verify_clock_model.py` loads only the original analysis's Kalman function
and compares production C states/rates with fixed R/q. Use a Python environment
with NumPy (on the development host `/usr/bin/python3`). Follower REF_RX logs lack
MCU uptime: that replay labels coarse age as inferred1Hz cadence; it is not a
validation of follower outage handling or physical accuracy.


## Bounded missing-reference recovery

Median-5 retains its original3000ms reset and five-interval warmup behavior.
Kalman retains a previously ready, same-epoch latent model for up to6000ms from
its last real reference observation. This is **not** a mapping-lifetime extension:
the existing2000ms freshness gate still rejects mapping/TDoA with an old origin.
No synthetic reference or timestamp is generated. Prediction uses actual elapsed
hardware TX time, with unchanged F/Q/R, Huber weighting and Joseph covariance.

On a returning packet after3..6s, the wrapper checks reference identity, increasing
modular sequence bounded by the configured period, TX separation consistent with
that sequence within the existing10% interval tolerance, positive local RX
separation and the original±200000ppb rate gate. Covariance/innovation/uncertainty
checks still run. Only a valid real observation replaces the mapping origin. The
independent median history restarts; it cannot become a ready fallback merely
because KF was retained. KF keeps its prior maturity across a validated gap.
The latent phase/rate/drift and covariance are not reseeded on that path.

After6s, nonmonotonic sequence, implausible timestamp, epoch change, numerical
failure or compensation-coordinate switch, the existing reset/rejection behavior
remains. PSD/finite checks, the1000ppb conditional sigma ceiling and gross RAW-origin
innovation gate remain enforced. AA propagation keeps its original measurement
age and epoch: a temporary radio-search state no longer invalidates a still-fresh
measurement when the clock model is eligible for recovery, but an expired/changed-
epoch measurement is never refreshed or used by this mechanism. TWR operations
are still canceled on reference loss. RF scheduling and packet formats are unchanged.

`recovered` occupies the last padding byte (fit state remains160 B). It marks a
retained gap history, not guaranteed validity; inspect ready/applied, fallback,
map status/age and reset reason together. STM32 `clock show` reports
holdover/recovered and live mapping status; ALR `:diag` reports Kalman/TWR progress
and optional debug SL1 records show worker execution. Existing raw sequence/TX/RX telemetry
identifies observed gaps, without claiming they were antenna-level RF losses.
No EEPROM/configuration field or telemetry schema changed.

Host tests cover1..6s intervals (up to five missed1Hz references), drift with
repeated bursts, wrap, stale mapping before return, covariance failure, timestamp
corruption and>6s reset/reacquisition. The receiver comparison tool replays old and
new C estimators on identical captured snapshots, keeping original follower
relations fixed and explicitly cold-starting at capture entry. Follower log replay
uses real TX/RX but inferred1Hz coarse age and no propagation compensation; it is
not physical accuracy validation. Missing packets/relations remain missing.

## Reference restart evidence and epoch ownership

The ALR follower used to rebase its scheduler after the 3000ms watchdog while
retaining a ready Kalman fit for 6000ms. The raw clock's sequence gate still
rejected the restarted reference's lower sequence. Broadcast slots therefore
continued from fresh RX timestamps while Kalman observations stayed ignored and
AR preparation used an expired mapping. A restart before the watchdog could
instead leave both sequence consumers rejecting the new stream.

Reference owners now retain one bounded `mulReferenceEvidence_t` candidate.
A backward sequence (or a changed timestamp at the same sequence) needs two
coherent reference packets. The existing integer interval gate checks modular
sequence advance, remote TX separation, positive local RX separation and the
200ppm rate limit, with elapsed time below 3000ms. Local DTU separation must also
agree with coarse monotonic elapsed time within 20ms; uptime never supplies a
clock ratio or a TX target. Duplicate candidates cannot refresh their age.
Normal forward packets discard tentative restart evidence. Both uint32 sequence
and native 40-bit timestamp rollover retain their normal modular meaning.

A confirmed restart resets the raw clock epoch, cancels queued fitting, replaces
the scheduler baseline and invalidates prepared reservations, TWR exchanges,
propagation and AA calibration. Begin reseeds Kalman against the new epoch before
any prior can be published. Finish/Prior bind pending work to its original 64-bit
epoch; Finish also rejects a superseded sequence. The ALR watchdog drops pending
work rather than retaining an incompletely committed observation as holdover.
Ordinary continuous gaps retain the existing 2s mapping/3s median/6s latent-Kalman
limits and original covariance/innovation checks. No holdover broadcast is made.

STM32 multi and legacy receivers use the same evidence gate. Followers cannot
rebase the reference cycle or keep restart rejection alive by refreshing a
cycle-level timestamp. Confirmation discards old vectors/associations and TWR
state; transported follower models remain unavailable until their follower epoch
changes. Retired follower models cannot advance the new cycle. New reference
observations warm the local estimator; fresh follower models/calibration are
also required before TDoA becomes available. Missing values remain invalid.

No reference boot/session token exists in RF5. A paced replay of two coherent old
packets is indistinguishable from a real restarted timeline. A restart that
coincidentally satisfies all old timeline continuity gates is also ambiguous.
Same-sequence/same-TX restarts are detected only once further observations expose
a discontinuity; a lone packet cannot prove restart. Reused follower epoch tokens
across a follower reboot can keep the receiver fail-closed until a distinct token
is observed. These limits cannot be eliminated by a sequence heuristic. A future
negotiated RF version could carry a reference boot/session token in reference
broadcasts and bind follower relations/TWR to it; no such extension is implemented.
Existing RF5 bytes, EEPROM codecs and plan fingerprints remain unchanged.

Deterministic tests exercise production follower events and the portable STM32
receiver with the 100ms/5ms/10ms-budget/reply10/window50 codec5 plan `2359421F`:
1/4/10s interruptions, continuous loss, RF sequence restart and TX-clock reboot,
both followers and estimators, repeated restarts, pending work/armed TX/reservations,
retired TDoA models, invalid values and recovery. Existing suites cover reordered
frames, duplicates, modular counter wraps and complete AA/AR exchanges. Software
checks do not validate board timing, RF recovery or physical ranging accuracy.

## Optional deferred observation on the LPS follower

The synchronous `mulClockObserveFitted` API retains its numerical behavior. Its
work is split into `mulClockFitBegin` and `mulClockFitFinish`; the prediction,
Joseph update, R/q, Huber rule, finite/LDL checks, rounding and recovery equations
are unchanged. Callers must retain the same packet, raw RX marker and reception
uptime until Finish, and must not interleave another Begin on that state. Finish
consumes the pending flag before work, so a second Finish is a no-op. A reset or
epoch invalidation cancels obsolete pending work. The LPS owns one96-byte pending
observation; no queue or clock field is persisted or added to the RF protocol.

For a scheduled follower broadcast, Begin performs current-cycle integer clock
bookkeeping, continuity/epoch checks and compensation selection. Seed/reset cases
finish immediately. A regular matrix update is queued. The LPS may publish a
**current-time prior** using `mulClockFitPrior`: a forecast from the last completed
fit, anchored to the newly accepted raw observation. It is not the old origin or
an uncomputed posterior. For measured reference interval d and half-slot h:

- Frequency is `state[1] + (d+h)*state[2]`.
- Its prior variance is `P11 + 2*(d+h)*P12 + (d+h)^2*P22 +
  q*d*(d*d/3 + h*d + h*h)`.
- The completed fit must be ready, finite/PSD and in the same nonzero epoch.
  The forecast retains the200000ppb rate gate and1000ppb conditional sigma gate.
- The current compensated phase must pass the existing one-ppm gross-innovation
  gate against the prior phase prediction. A newly observed timestamp cannot be
  trusted solely because the previous frequency fit was good.

Compensation changes or discontinuities seed the filter before any forecast;
missing/expired propagation still makes relation correction unavailable through
the unchanged propagation checks. Forecast failure makes the pending broadcast's
relation unavailable; the explicitly selected Median-5 mode remains immediate.
No covariance is advertised as a physical bound. Mapping/propagation age fields
continue to refer to the current real origin and actual delay acquisition.

After confirmed hardware TX completion, or a safely aborted/skipped transmission,
Finish is eligible only when the radio owner has enough slack. It restores the
saved raw median status/rate before running the unchanged posterior calculation.
Thus post-completion state is bit-identical to synchronous processing of the same
observation. Publication can differ because the broadcast intentionally uses the
prior, not a posterior calculated before TX. Startup may require one additional
broadcast before a completed mature fit can authorize the prior. Synthetic
outlier trajectories show the expected higher forecast error; see the LPS
[diagnostic guide](../../docs/mul_aa_diagnostics.md#deferred-kalman-at-5ms) for
measurements, limitations and deployment commands.

Only the LPS opts into this two-phase API. Existing vendored STM32/ESP32 cores and
synchronous receiver replay remain compatible; no consumer firmware change is
required. Neither a saved plan nor its RF5 fingerprint changes.
