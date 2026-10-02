"""Harness self-tests on the example task: honest candidates pass, and
common cheats or bugs are caught."""

from pathlib import Path

import pytest

from kernelrl.task import Task, evaluate

EXAMPLE = Path(__file__).resolve().parents[1] / "examples" / "libyuv.ARGBAttenuateRow"
HEADER = '#include "kernel.h"\n'


@pytest.fixture(scope="module")
def task():
    return Task.load(EXAMPLE)


def _candidate(tmp_path, body):
    p = tmp_path / "cand.c"
    p.write_text(HEADER + body)
    return p


def test_reference_copy_is_correct_and_neutral(task):
    r = evaluate(task, trials=5)
    assert r["status"] == "ok"
    assert 0.8 < r["geomean_speedup"] < 1.25


def test_gold_is_correct(task):
    assert evaluate(task, EXAMPLE / "gold.c", mode="check")["status"] == "ok"


@pytest.mark.parametrize("mode", ["probe-noop", "probe-perturb"])
def test_probes_are_caught(task, mode):
    assert evaluate(task, mode=mode)["status"] == "incorrect"


def test_calling_reference_is_rejected(task, tmp_path):
    c = _candidate(tmp_path, "void ARGBAttenuateRow_opt(const uint8_t* s, uint8_t* d, int w)"
                             "{ ARGBAttenuateRow_ref(s, d, w); }\n")
    r = evaluate(task, c, mode="check")
    assert r["status"] == "build_error" and "ARGBAttenuateRow_ref" in r["error"]


def test_out_of_bounds_write_is_caught(task, tmp_path):
    c = _candidate(tmp_path, "#include <string.h>\n"
                             "void ARGBAttenuateRow_opt(const uint8_t* s, uint8_t* d, int w)"
                             "{ for (int i = 0; i < w * 4; i++) d[i] = i % 4 == 3 ? s[i] : (s[i] * s[i|3] + 255) >> 8;"
                             "  if (w > 0) d[w * 4] = 1; }\n")
    r = evaluate(task, c, mode="check")
    assert r["status"] == "incorrect"
    assert any(w["guard_violations"] for w in r["check"])


def test_wrong_tail_is_caught(task, tmp_path):
    # handles only multiples of 8 pixels, like a raw SIMD loop without a tail
    c = _candidate(tmp_path, "void ARGBAttenuateRow_opt(const uint8_t* s, uint8_t* d, int w)"
                             "{ for (int i = 0; i < (w & ~7) * 4; i++) d[i] = i % 4 == 3 ? s[i] : (s[i] * s[i|3] + 255) >> 8; }\n")
    r = evaluate(task, c, mode="check")
    assert r["status"] == "incorrect"
    assert {"w1923_tail", "w7"} <= {w["workload"] for w in r["check"] if not w["pass"]}
