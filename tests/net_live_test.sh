#!/usr/bin/env bash
set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/lib/run_qemu_test.sh"

failures=0

# Host-side echo servers; QEMU user networking maps guest 10.0.2.2 to host 127.0.0.1.
python3 - <<'PY' &
import socket, threading
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
u.bind(("127.0.0.1", 47001))
t = socket.socket()
t.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
t.bind(("127.0.0.1", 47002))
t.listen(1)
def udp():
    while True:
        data, addr = u.recvfrom(256)
        u.sendto(data.upper(), addr)
def tcp():
    while True:
        c, _ = t.accept()
        data = c.recv(256)
        c.sendall(data.upper())
        c.close()
threading.Thread(target=udp, daemon=True).start()
tcp()
PY
server=$!
trap 'kill ${server} 2>/dev/null' EXIT
sleep 0.5

export SOBI_QEMU_EXTRA="-netdev user,id=n0 -device virtio-net-pci,netdev=n0,disable-modern=on"

run_qemu_test net-live "test=net-live" "${QEMU_EXIT_PASS}" \
    "boot: ok" \
    "net: virtio-net up" \
    "net: udp echo from host: 'PING OVER UDP'" \
    "net: tcp echo from host: 'HELLO VIA TCP'" \
    "net: live checks passed" \
    || failures=$((failures + 1))

if [[ ${failures} -eq 0 ]]; then
    echo "net_live_test: all tests passed"
    exit 0
fi
echo "net_live_test: ${failures} test(s) failed"
exit 1
