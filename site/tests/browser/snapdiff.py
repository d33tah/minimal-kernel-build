"""Browser test suite snapdiff (test_browser.py --suites)."""

import time

from tests.browser.common import report, snapdiff_guard
from tests.browser.snapdiff_run import snapdiff_run
from tests.browser.snapdiff_more import (
    onecount_run, snapdiff_bigsave, snapdiff_kernels, snapdiff_legacy, snapdiff_perf, snapdiff_poweron,
    snapdiff_replay, snapdiff_storage,
)
from tests.browser.snapdiff_corrupt import snapdiff_corrupt, snapdiff_edges


# R3: saved at 1.3 M, hello's SeaBIOS reads the TSC before 1.4 M (its
# calibration). v86's set_tsc left a restored TSC off by the offset the
# machine had before the Restore, and SeaBIOS's data then differed (RAM
# after the 100,000 steps).
SNAP_REPLAY_TSC = (1_300_000, 100_000)


def snapdiff_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if linux:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(snapdiff_run, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(snapdiff_storage, browser, base, shots)
    out.append(report("snapdiff storage", problems, r))
    problems, r = snapdiff_guard(snapdiff_legacy, browser, base, shots)
    out.append(report("snapdiff S1 gzip", problems, r))
    problems, r = snapdiff_guard(snapdiff_edges, browser, base, shots)
    out.append(report("snapdiff edges", problems, r))
    problems, r = snapdiff_guard(snapdiff_corrupt, browser, base, shots)
    out.append(report("snapdiff corrupt", problems, r))
    for phone in (False, True):
        problems, r = snapdiff_guard(snapdiff_bigsave, browser, base, shots, phone)
        out.append(report("snapdiff big save", problems, r, width=390 if phone else 1280))
    pw = [("hello", "light", False), ("hello", "dark", True)] + ([("minified-linux", "dark", False)] if linux else [])
    for name, scheme, phone in pw:
        problems, r = snapdiff_guard(snapdiff_poweron, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff power-on", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    rp = [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else [])
    for name, scheme, phone in rp:
        problems, r = snapdiff_guard(snapdiff_replay, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff replay", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    problems, r = snapdiff_guard(snapdiff_replay, browser, base, shots, "hello", "light", False, SNAP_REPLAY_TSC)
    out.append(report("snapdiff replay tsc", problems, r, kernel="hello", scheme="light", width=1280))
    for phone in (False, True):
        problems, r = snapdiff_guard(snapdiff_kernels, browser, base, shots, phone)
        out.append(report("snapdiff kernels", problems, r, width=390 if phone else 1280))
    for name, scheme, phone in [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else []):
        problems, r = snapdiff_guard(onecount_run, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff one count", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    for mem in (2 << 20, 64 << 20):
        problems, r = snapdiff_guard(snapdiff_perf, browser, base, shots, mem)
        out.append(report("snapdiff perf", problems, r, mem=mem))
    return any(out)


def suite_snapdiff(ctx):
    return snapdiff_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)
