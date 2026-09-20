#!/usr/bin/env bash
# gate4.sh — Phase 4 strict gate runner. Runs every scripted gap check in
# order, collects PASS/FAIL, writes tools/verification_report_<date>.log.
# Manual checklist (gaps 7/10/11) is NOT run here — see LIVE_TEST_CHECKLIST.md.
#
# Usage: bash tools/gate4.sh [--skip-slow]
set -u
cd "$(dirname "$0")/.."
REPORT="tools/verification_report_$(date +%Y%m%d_%H%M).log"
SKIP_SLOW=0
[ "${1:-}" = "--skip-slow" ] && SKIP_SLOW=1

log() { echo "$@" | tee -a "$REPORT"; }
run() { # run <name> <cmd...>
  local name="$1"; shift
  log "== $name =="
  if "$@" >>"$REPORT" 2>&1; then log "[PASS] $name"; else log "[FAIL] $name"; FAILED=1; fi
}

FAILED=0
log "Gate 4 started $(date)  report=$REPORT"

run "0a verify suite"      bash tools/verify_linux_agent.sh
run "0b six-transport e2e" python3 tools/e2e.py all --timeout 120
run "0c matrix chain"      bash tools/gate3_matrix.sh

run "gap1 reconnect storm" bash tools/reconnect_storm_test.sh
run "gap2 rotation boundary" bash tools/rotation_test.sh
run "gap3 multi-agent"     bash tools/multiagent_test.sh
run "gap4 crypto edges"    bash tools/crypto_edge_test.sh
[ "$SKIP_SLOW" = 0 ] && run "gap5 periodogram" python3 tools/periodogram_test.py --log /tmp/ax_rot_test.log || log "[SKIP] gap5 (--skip-slow)"
[ "$SKIP_SLOW" = 0 ] && run "gap6 entropy" python3 tools/entropy_test.py --samples /tmp/e2e_work/edge_* || log "[SKIP] gap6 (--skip-slow)"
run "gap8 rotation leak"   bash tools/leak_test.sh
run "gap9 arm64 smoke"     bash tools/arm64_smoke_test.sh
run "gap12 JA4 no-regress" python3 tools/p31_ja4_regress.py

log ""
if [ "$FAILED" = 0 ]; then
  log "GATE 4: ALL PASS"
else
  log "GATE 4: FAILURES PRESENT — fix before Phase 5"
fi
exit "$FAILED"
