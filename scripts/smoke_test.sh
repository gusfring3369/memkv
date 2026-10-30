#!/usr/bin/env bash
# End-to-end compatibility check: drives a real memkv-server with the stock redis-cli.
#
# Usage: scripts/smoke_test.sh [path/to/memkv-server]
set -euo pipefail

SERVER=${1:-build/memkv-server}
PORT=${PORT:-6399}
CLI=(redis-cli -p "$PORT")

"$SERVER" --port "$PORT" 2>/dev/null &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

for _ in $(seq 50); do
    "${CLI[@]}" PING >/dev/null 2>&1 && break
    sleep 0.1
done

failures=0
expect() {
    local expected=$1
    shift
    local actual
    actual=$("${CLI[@]}" "$@" 2>&1 | tr '\n' ' ' | sed 's/[[:space:]]*$//')
    if [[ "$actual" == "$expected" ]]; then
        echo "ok    $*"
    else
        echo "FAIL  $*  -> expected '$expected', got '$actual'"
        failures=$((failures + 1))
    fi
}

expect "PONG" PING
expect "OK" SET user:101 Alice
expect "Alice" GET user:101
expect "OK" SET otp:999 482109 PX 100
expect "" GET missing
expect "5" ZADD leaderboard 1250.5 Player_One 890 Player_Two 2400 Pro_Gamer 1540 Mid_Tier 1720 Sniper_99
expect "Player_One 1250.5 Mid_Tier 1540 Sniper_99 1720" ZRANGEBYSCORE leaderboard 1200 1800 WITHSCORES
expect "Pro_Gamer" ZRANGE leaderboard -1 -1
expect "4" ZRANK leaderboard Pro_Gamer
expect "zset" TYPE leaderboard
expect "WRONGTYPE Operation against a key holding the wrong kind of value" GET leaderboard
sleep 0.2
expect "" GET otp:999
expect "2" DBSIZE

if ((failures > 0)); then
    echo "$failures check(s) failed"
    exit 1
fi
echo "all checks passed"
