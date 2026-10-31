#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/network
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/net-rtl.c src/net_rtl.c src/platform.c -pthread -o build/network/test-rtl
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/network/test-rtl
clang -std=gnu11 -D_DARWIN_C_SOURCE -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude tools/network-probe.m src/net_macos.m src/platform.c -pthread -framework Foundation -framework vmnet -o build/network/network-probe
clang -std=gnu11 -D_DARWIN_C_SOURCE -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude tools/network-helper.m src/net_macos.m src/platform.c -pthread -framework Foundation -framework vmnet -o build/network/ptolemy-network-helper
clang -std=gnu11 -D_DARWIN_C_SOURCE -fobjc-arc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/net-transport.m src/net_macos.m src/platform.c -pthread -framework Foundation -framework vmnet -o build/network/test-transport
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/network/test-transport
python3 tools/check_network_board.py
echo 'Network checks passed; no vmnet interface or privileged helper started.'
