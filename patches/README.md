# Project-local ESP-IDF 6.1 stability patches

The top-level CMakeLists.txt replaces exactly two SDK compilation units with
these checked-in copies. The installed SDK is not edited. Compiler-command tests
verify that each patched unit is compiled once and its original is not compiled.
The existing component dependencies/includes remain in effect. ESP-IDF versions
other than 6.1 are rejected until these patches are reviewed.

- EMAC DMA: bound missing-terminal-descriptor walks by the RX ring size; drop
  malformed/partial frames without looping forever; reject CRC length underflow.
- Ping: observe stop requests and absolute receive budgets despite unrelated raw
  ICMP packets; make shared session flags atomic. Packet counts, payloads,
  configured timeouts, interfaces and normal pacing are unchanged.

Both copies retain the upstream Apache-2.0 SPDX notices. The accompanying patch
files show the exact modifications against the installed SDK. On an SDK update,
review/rebase these copies instead of silently relying on the old implementation.
Upstream source SHA256 when these fixes were prepared:

- `C:/esp/v6.1/esp-idf/components/esp_eth/src/mac/esp_eth_mac_esp_dma.c`: `e6c6f5af02448fc6cd70b8a2ef9c39aa04422e5dc5a40d1f4e1f085b76cde7c7`
- `C:/esp/v6.1/esp-idf/components/lwip/apps/ping/ping_sock.c`: `39b4be760a2137681f9f07d077c69b8de0cc1d88bed79ccfafaa1814d1d68fa5`
