# Five-distance additive ADS calibration analysis

Proposal only: calibration remains disabled in firmware and in the exported configuration.
No scale factor is estimated. References are manually measured antenna separations,
not certified ground truth. Counter accumulation in the 2 m capture is operator-reported
reuse without an ALR reset; accumulated counters are not used as a defect or fit criterion.

Estimator: negative median of per-run median(raw_m - reference_m), 5 equally weighted runs.
Unrounded correction: -0.468600000 m; proposed correction_mm=-469.
Reference-uncertainty assumption: ±0.100 m per run; illustrative, not verified or a statistical CI.
A common reference error shifts the proposed correction by the same amount; more samples
do not reduce that systematic uncertainty. Distance-dependent residuals may reflect reference
error, orientation, propagation or radio bias; this analysis cannot identify their cause.

Validity: unique ordered session/transaction RESULTS with preceding six timestamps, independent
40-bit ADS recomputation, bounded 100 ms intervals, ±1000 ppm ratio, firmware agreement,
finite/nonnegative raw range, firmware valid status when present, and range ≤10 m.
TIMES-only candidates, duplicates, malformed/nonfinite/negative/invalid results are excluded
explicitly and retained in the sample export. MAD/jump annotations alone do not exclude samples.
Existing software offsets/calibrated output are never used as the fit input.

| Reference m | Audited valid / rows | Raw median m | Bias m | SD m | MAD m | Corrected median residual m | p95 absolute residual m |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.5 | 272/274 | 0.986700 | 0.486700 | 0.047961 | 0.031350 | 0.017700 | 0.116615 |
| 1 | 356/357 | 1.463100 | 0.463100 | 0.057820 | 0.035200 | -0.005900 | 0.078225 |
| 2 | 247/248 | 2.420200 | 0.420200 | 0.040151 | 0.028100 | -0.048800 | 0.103520 |
| 3 | 303/303 | 3.468600 | 0.468600 | 0.014350 | 0.010100 | -0.000400 | 0.026690 |
| 5 | 523/523 | 5.569200 | 0.569200 | 0.013513 | 0.008400 | 0.100200 | 0.123390 |

Input audit and exclusions (flag counts may overlap):

- `ADS-050cm-ALR.log` SHA256 `9cfddad1508e8217eab0163647b85dbcdd9713778dd96ba620f97dccc80ab1dd`: 1 epoch(s), 2 excluded sample rows, 1 malformed records; flags {"firmware_invalid": 1, "large_jump": 1, "missing_result": 1, "negative": 1, "ratio_outlier": 1}.
- `ADS-100cm-ALR.log` SHA256 `a208f7a6da9fc737836a71c6b66b926c0b65aaa8fb203bfe50ddffb2a3ecdd9e`: 3 epoch(s), 1 excluded sample rows, 1 malformed records; flags {"missing_result": 1}.
- `ADS-200cm-ALR.log` SHA256 `903da1e10d55d5483775d79336b9888680bdb08cc0021b40a93cc093f222c5bf`: 1 epoch(s), 1 excluded sample rows, 1 malformed records; flags {"firmware_invalid": 1, "large_jump": 1, "negative": 1, "ratio_outlier": 1}.
- `ADS-300cm-ALR.log` SHA256 `1bf99e513cb2874fe6d12f111d800d374b2bfd3c32d2ee14d4b95c74e06c9650`: 1 epoch(s), 0 excluded sample rows, 0 malformed records; flags {}.
- `ADS-500cm-ALR.log` SHA256 `c18ad5a1bb67dcc3f7e1c60e40083c7c195633fabc1c308838c690d1afb3839c`: 1 epoch(s), 0 excluded sample rows, 0 malformed records; flags {}.

Residuals reuse the fitting data and are not held-out accuracy validation. Hardware antenna
delays and software offsets must stay unchanged between fitting and validation. The logs
describe one mixed-radio pair/configuration; do not copy its offset to every robot.
Repeat with accurately measured separations, independent sessions, opposite radio role,
and a held-out distance before explicitly enabling calibration. No hardware was flashed.
