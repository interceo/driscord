#!/usr/bin/env bash

set -euo pipefail

IFACE="${DRISCORD_IFACE:-lo}"
CMD="${1:-}"

if [[ $EUID -ne 0 ]]; then
    exec sudo -E "$0" "$@"
fi

teardown() {
    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    echo "[net_emulate] Removed tc rules from $IFACE."
}

show() {
    echo "[net_emulate] Current tc state on $IFACE:"
    tc qdisc show dev "$IFACE"
    tc filter show dev "$IFACE" 2>/dev/null || true
}

apply_udp_netem() {
    local netem_opts="$1"

    tc qdisc del dev "$IFACE" root 2>/dev/null || true

    tc qdisc add dev "$IFACE" root handle 1: prio bands 3 \
        priomap 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1

    # shellcheck disable=SC2086
    tc qdisc add dev "$IFACE" parent 1:1 handle 10: netem $netem_opts

    tc filter add dev "$IFACE" parent 1: protocol ip prio 1 \
        u32 match ip protocol 17 0xff flowid 1:1

    echo "[net_emulate] Applied netem ($netem_opts) on UDP/$IFACE."
}

setup_clean() {
    apply_udp_netem "delay 0ms"
}

setup_degraded() {
    apply_udp_netem "delay 30ms 10ms distribution normal loss 2%"
}

setup_bad() {
    apply_udp_netem "delay 80ms 30ms distribution normal loss 8% reorder 5% gap 5"
}

setup_terrible() {
    apply_udp_netem "delay 150ms 60ms distribution normal loss 15% reorder 10% gap 5 duplicate 3%"
}

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
