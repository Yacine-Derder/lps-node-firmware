#!/usr/bin/env python3
"""Offline Cortex-M0 instruction comparison; not board cycles, latency or WCET.
Uses existing Unicorn/Capstone/pyelftools instrumentation and real C paths.
"""
import json
from pathlib import Path
import tempfile
from clock_cost import build, measure, ROOT

with tempfile.TemporaryDirectory(prefix='mul-motion-cost-') as temp:
    result = {}
    for guard in (False, True):
        name = 'arrival_guard' if guard else 'unconditional_kalman'
        samples, _ = measure(build(Path(temp), name,
                                   ROOT/'portable/mul_core/mul_clock_fit.c', 200, guard))
        result[name] = dict(max_observed_instructions=max(x['instructions'] for x in samples),
                            regular_max_instructions=max(x['instructions'] for x in samples if x['kind'] == 1),
                            max_double_helper_calls=max(sum(x['double_helpers'].values()) for x in samples),
                            calls=len(samples))
    result['interpretation'] = '16 deterministic Cortex-M0 calls at 200ms; instruction counts only; not hardware WCET'
    print(json.dumps(result, indent=2))
