#!/usr/bin/env bash
# net_emulate.sh — Wrap Linux tc/netem to emulate network degradation for
# WebRTC UDP traffic. Only UDP (ip protocol 17) is affected; TCP (signaling
# WebSocket, API) is left untouched.
#
# Usage:
#   ./scripts/net_emulate.sh setup  <profile>   # degraded | bad | terrible | clean
#   ./scripts/net_emulate.sh show               # print current qdisc state
#   ./scripts/net_emulate.sh teardown           # remove all rules
#
# Interface defaults to 'lo' (loopback). Override:
#   DRISCORD_IFACE=eth0 ./scripts/net_emulate.sh setup bad
#
# Root: the script auto-reinvokes itself via sudo -E when not already root,
# so you do not need to pre-escalate in test scripts.

set -euo pipefail

IFACE="${DRISCORD_IFACE:-lo}"
CMD="${1:-}"

# ---------------------------------------------------------------------------
# Auto-escalate to root if needed.
# ---------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    exec sudo -E "$0" "$@"
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
teardown() {
    # Remove root qdisc (suppresses error if nothing is installed).
    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    echo "[net_emulate] Removed tc rules from $IFACE."
}

show() {
    echo "[net_emulate] Current tc state on $IFACE:"
    tc qdisc show dev "$IFACE"
    tc filter show dev "$IFACE" 2>/dev/null || true
}

# Apply netem impairment on UDP traffic only via a prio qdisc + u32 filter.
# $1 = netem options string (e.g. "delay 30ms 10ms distribution normal loss 2%")
apply_udp_netem() {
    local netem_opts="$1"

    # Remove any existing root qdisc first.
    tc qdisc del dev "$IFACE" root 2>/dev/null || true

    # prio qdisc: 3 bands. Band 1 gets netem; bands 2-3 get pfifo (best-effort).
    tc qdisc add dev "$IFACE" root handle 1: prio bands 3 \
        priomap 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1

    # Attach netem to band 1 (handle 10:).
    # shellcheck disable=SC2086
    tc qdisc add dev "$IFACE" parent 1:1 handle 10: netem $netem_opts

    # u32 filter: match UDP (ip protocol 17, 0xff mask) → band 1.
    tc filter add dev "$IFACE" parent 1: protocol ip prio 1 \
        u32 match ip protocol 17 0xff flowid 1:1

    echo "[net_emulate] Applied netem ($netem_opts) on UDP/$IFACE."
}

# ---------------------------------------------------------------------------
# Profile definitions
# ---------------------------------------------------------------------------
setup_clean() {
    apply_udp_netem "delay 0ms"
}

setup_degraded() {
    # 30ms ± 10ms delay (normal distribution), 2% loss.
    apply_udp_netem "delay 30ms 10ms distribution normal loss 2%"
}

setup_bad() {
    # 80ms ± 30ms delay, 8% loss, 5% reorder (gap 5 packets).
    apply_udp_netem "delay 80ms 30ms distribution normal loss 8% reorder 5% gap 5"
}

setup_terrible() {
    # 150ms ± 60ms delay, 15% loss, 10% reorder, 3% duplicate.
    apply_udp_netem "delay 150ms 60ms distribution normal loss 15% reorder 10% gap 5 duplicate 3%"
}

# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------
case "$CMD" in
    setup)
        PROFILE="${2:-}"
        case "$PROFILE" in
            clean)     setup_clean     ;;
            degraded)  setup_degraded  ;;
            bad)       setup_bad       ;;
            terrible)  setup_terrible  ;;
            *)
                echo "Usage: $0 setup <clean|degraded|bad|terrible>" >&2
                exit 1
                ;;
        esac
        ;;
    show)
        show
        ;;
    teardown)
        teardown
        ;;
    *)
        echo "Usage: $0 <setup <profile>|show|teardown>" >&2
        echo "  Profiles: clean  degraded  bad  terrible" >&2
        echo "  Override interface: DRISCORD_IFACE=eth0 $0 setup bad" >&2
        exit 1
        ;;
esac
