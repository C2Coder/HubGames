#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
output_dir=${1:-/tmp/hubgames-host}
mkdir -p "$output_dir"
compiler=${CC:-clang}
sanitizers=${HOST_SANITIZERS:-address,undefined}
flags="-std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=$sanitizers -fno-omit-frame-pointer -fno-sanitize-recover=all"
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_host.c tests/host_support.c main/gfx.c main/fx.c main/ambient.c main/school.c main/games/*.c -o "$output_dir/test_host" -lm
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_arena.c tests/host_support.c main/gfx.c main/fx.c -o "$output_dir/test_arena" -lm
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_fps.c tests/host_support.c main/gfx.c main/fx.c -o "$output_dir/test_fps" -lm
# shellcheck disable=SC2086
"$compiler" $flags -std=gnu11 -DTEST_NETWORK -Itests/stubs/net -Itests/stubs -Imain -Imain/games tests/test_net.c tests/host_support.c -o "$output_dir/test_net" -lm
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_tanks.c tests/host_support.c main/gfx.c main/fx.c -o "$output_dir/test_tanks" -lm
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_racers.c tests/host_support.c main/gfx.c main/fx.c -o "$output_dir/test_racers" -lm
# shellcheck disable=SC2086
"$compiler" $flags -Itests/stubs -Imain -Imain/games tests/test_towers.c tests/host_support.c main/gfx.c main/fx.c -o "$output_dir/test_towers" -lm
"$output_dir/test_tanks"
"$output_dir/test_racers"
"$output_dir/test_towers"
"$output_dir/test_net"
"$output_dir/test_arena"
"$output_dir/test_fps"
"$output_dir/test_host" "$output_dir"
