# ADS-TWR capture analysis

Approximate reference: 1 m (not certified ground truth).

Selection: raw statistics include every finite parsed RESULT, including negatives. Primary requires unique preceding TIMES in the same file/epoch/session/id, full40-bit markers, positive <=100ms modular intervals, ratio within +/-1000ppm, independent agreement within0.00001DTU/0.0000501m/0.000000000501ratio, nonnegative range/ToF and no firmware invalid flag, with range <= 10m (explicit laboratory bound). Ambiguous duplicate IDs and out-of-order results are excluded. Robust additionally excludes |range-median(primary)| > max(3*1.4826*MAD,0.05m). Jumps > 0.5m and ratio deviations > max(6*1.4826*MAD,1ppm) are annotations; they alone do not exclude a following good sample. All RESULT samples/flags and unmatched TIMES candidates remain in CSV; TIMES_ONLY candidates are excluded from range statistics.

SD is population SD. Percentiles use linear interpolation at (n-1)*p. RMSE/bias use the approximate reference. No counter or measurement is pooled across files/epochs. Unmatched counter windows cannot establish packet delivery or UART/range causation.

## ADS-ALR-initiator.log (initiator)

SHA-256: `cd88d5e2cf085f7984bb7325453fc74ef86468692f4f30165a533afa985d5459`.

Malformed/truncated records: 1; concatenated lines: [25].
- Line25: incomplete_stat.

### Epoch 1, session 0000000078C3DCF1, origin init

Printed RESULTS=219, TIMES=219, TIMES without RESULT=0.

| Counter | First snapshot | Last snapshot | Modulo32 delta |
|---|---:|---:|---:|
| attempt | 1 | 550 | 549 |
| done | 0 | 462 | 462 |
| abort | 0 | 87 | 87 |
| reject | 0 | 0 | 0 |
| invalid | 0 | 0 | 0 |
| err | 0 | 249 | 249 |
| to | 19 | 10334 | 10315 |

Last cumulative snapshot: completed/resolved=462/549 (84.153005%); aborted/resolved=87/549 (15.846995%). These are endpoint lifetime ratios, not cross-file delivery rates or necessarily capture-only rates.
Within first/last snapshot lines22–685: done/attempt flow=462/549 (84.153005%), abort/attempt flow=87/549 (15.846995%). Endpoint stages=1/1; in-flight boundary exchanges can differ; this is not a closed transaction cohort.
1 RESULT record(s) follow the last snapshot (IDs: 550); last done counter is therefore a lower bound at capture end.

Model-checked closed initiator cohort IDs326–550: completed=219/225 (97.333333%), aborted=6/225 (2.666667%). Uses INIT, id==attempt, entry pending-POLL counters, final RESULT after pending-POLL snapshot, unique paired records and exact completed+aborted conservation. Completed includes negative raw ToF. This is one initiator transaction cohort, not a cross-radio PDR.

| Statistic | All finite raw | Primary | Robust |
|---|---:|---:|---:|
| n | 219 | 218 | 213 |
| mean | 1.33756667 | 1.3500711 | 1.34790751 |
| median | 1.342 | 1.34205 | 1.3419 |
| sd_population | 0.187778655 | 0.0343444057 | 0.0315839613 |
| bias_vs_approx_reference | 0.337566667 | 0.350071101 | 0.347907512 |
| rmse_vs_approx_reference | 0.386279792 | 0.351751779 | 0.349338208 |
| minimum | -1.3884 | 1.2764 | 1.2764 |
| p1 | 1.286182 | 1.294334 | 1.294324 |
| p5 | 1.30473 | 1.3068 | 1.3063 |
| p25 | 1.32535 | 1.32595 | 1.325 |
| p50 | 1.342 | 1.34205 | 1.3419 |
| p75 | 1.36905 | 1.369425 | 1.3665 |
| p95 | 1.41448 | 1.41452 | 1.4071 |
| p99 | 1.43767 | 1.437755 | 1.42314 |
| maximum | 1.4716 | 1.4716 | 1.4245 |

Primary MAD=0.01915m; robust exclusion threshold=0.08517537m.
Clock ratio (all finite parsed): n=219, mean=1.00001014, median=1.00001002, sd_population=1.69278528e-06, minimum=1.00000967, maximum=1.00003509, p1=1.00000983, p5=1.00000987, p25=1.00000996, p50=1.00001002, p75=1.00001008, p95=1.0000102, p99=1.00001026.
Rows with modular timestamp-wrap intervals: 0.
Across-result local t1/t2 decreases: 26 (consistent with timestamp wraps or resets, not proof of either alone).
Maximum independent absolute residual: tof_error_dtu=4.99042301e-07, metres_error=4.95088171e-05, ratio_error=4.99268182e-10.
Independent math/unit/ratio mismatches: 0.
Missing RESULT IDs between printed results: 6 (aborts or logging omissions; not RF loss).

| ID | Range m | Flags |
|---|---:|---|
| 361 | 1.4392 | range_mad_outlier |
| 362 | -1.3884 | large_jump, negative, ratio_outlier |
| 363 | 1.3416 | large_jump |
| 374 | 1.4307 | range_mad_outlier |
| 462 | 1.4410 | range_mad_outlier |
| 503 | 1.4716 | range_mad_outlier |
| 536 | 1.4287 | range_mad_outlier |

Visible UART fault reports=0; STOP acknowledgements=0; watchdog/heartbeat-loss reports=0; console drop reports=0 (reported interval message drops=0). Missing reports do not establish zero faults.

Recorded events (cumulative fault counters are not independent events):

- Line21: `MULH INIT radio=DW1000 role=1 session=0000000078C3DCF1 ant=16384 ch=5 version=1 bytes=56 uncalibrated=1`
## ADS-RLR-responder.log (responder)

SHA-256: `5bd3c8b589f81a1046af36430564d0d76c022960230bd51a00a65185dc4634f9`.

Malformed/truncated records: 0; concatenated lines: none.

### Epoch 1, session unknown, origin partial_capture

Printed RESULTS=0, TIMES=0, TIMES without RESULT=0.

| Counter | First snapshot | Last snapshot | Modulo32 delta |
|---|---:|---:|---:|
| attempt | 258 | 483 | 225 |
| done | 244 | 468 | 224 |
| abort | 14 | 15 | 1 |
| reject | 0 | 0 | 0 |
| invalid | 0 | 0 | 0 |
| err | 744 | 1098 | 354 |
| to | 4841 | 8988 | 4147 |

Last cumulative snapshot: completed/resolved=468/483 (96.894410%); aborted/resolved=15/483 (3.105590%). These are endpoint lifetime ratios, not cross-file delivery rates or necessarily capture-only rates.
Within first/last snapshot lines1–234: done/attempt flow=224/225 (99.555556%), abort/attempt flow=1/225 (0.444444%). Endpoint stages=0/0; both idle, so no boundary in-flight exchange.

Visible UART fault reports=1; STOP acknowledgements=0; watchdog/heartbeat-loss reports=0; console drop reports=0 (reported interval message drops=0). Missing reports do not establish zero faults.

Recorded events (cumulative fault counters are not independent events):

- Line107: `[EPUCK] FAULT uart=5 bits=0x8 drops=14 bad_lines=3 data_bad=0`
- Line108: `[STREAM] OK STREAM acknowledged=0 ms desired=0 ms`

Radio error counts include idle conditions; frame-wait timeouts are expected between1Hz exchanges. Old firmware has no abort-cause counters, so exact failed stages cannot be reconstructed. Responder done means REPORT TX; initiator done means computed exchange, including negative raw ToF in legacy firmware. Legacy TIMES has no session tag; unknown-session records remain explicitly unknown, and no cross-file association is attempted.
