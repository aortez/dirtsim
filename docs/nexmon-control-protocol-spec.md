# DirtSim Nexmon Channel Control: Clean-Room Protocol Notes (dirtsim3.local)

This document is a **clean-room, black-box** protocol note for controlling the Nexmon “scanner stack” channel on `dirtsim3.local`.

Constraints followed while producing this spec:

- No Nexmon source, vendored Nexmon files, or external Nexmon docs were consulted.
- No disassembly and no `strings`/`objdump`/`nm` were used on any binaries.
- Evidence comes only from runtime-visible output and `strace` syscall traces on the device.

## Scope

Goal: document enough of the runtime control protocol to later implement an in-process `NexmonChannelController` capable of:

- setting a **20 MHz** channel (observed example: `44/20`),
- optionally reading back the current channel (via `nexutil -k` behavior),
- optionally noting monitor/promisc commands (observed via `nexutil -m2` / `nexutil -p1`).

Non-goals:

- Explaining Nexmon internals.
- Documenting channels other than 20 MHz.
- Documenting any “objmem”/memory dump functionality.

## Evidence Collection (Exact Commands Run)

All commands were executed against `dirtsim3.local` over SSH.

### 1) Confirm initial mode (stock stack)

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo /usr/bin/dirtsim-nexmon-mode status; iw dev"
```

Observed (excerpt):

```text
loaded_version=none
monitor_supported=0
stack=stock
...
Interface wlan0
...
channel 40 (5200 MHz), width: 80 MHz, center1: 5210 MHz
```

### 2) Enter Nexmon scanner stack

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo /usr/bin/dirtsim-nexmon-mode enter"
```

Observed:

```text
loaded_version=6.12.2-nexmon
monitor_supported=1
stack=nexmon
```

### 3) Capture syscall traces for set/get

Set channel to `44/20`:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-set.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil --chanspec=44/20"
```

Get current channel/chanspec:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-get.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil -k"
```

Observed stdout from the `-k` invocation:

```text
chanspec: 0xd02c, 44
```

Verify effect:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local "iw dev"
```

Observed (excerpt):

```text
Interface wlan0
...
channel 44 (5220 MHz), width: 20 MHz, center1: 5220 MHz
```

### 4) Optional: monitor/promisc traces

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-m2.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil -m2"

ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-p1.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil -p1"
```

After running both, `iw dev` still reported `type managed` and `channel 44` (no visible interface-type change was observed via `iw dev` alone).

### 5) Copy traces back (if needed)

```bash
scp dirtsim3.local:/tmp/nexutil-set.trace /tmp/dirtsim3-nexutil-set.trace
scp dirtsim3.local:/tmp/nexutil-get.trace /tmp/dirtsim3-nexutil-get.trace
scp dirtsim3.local:/tmp/nexutil-m2.trace  /tmp/dirtsim3-nexutil-m2.trace
scp dirtsim3.local:/tmp/nexutil-p1.trace  /tmp/dirtsim3-nexutil-p1.trace
```

### 6) Restore stock stack (always)

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo /usr/bin/dirtsim-nexmon-mode exit; sudo /usr/bin/dirtsim-nexmon-mode status; iw dev"
```

Observed:

```text
loaded_version=none
monitor_supported=0
stack=stock
```

## Preconditions for Success (Observed)

### Nexmon stack must be enabled

When `stack=stock` (from `dirtsim-nexmon-mode status`), running `nexutil` operations that use the netlink transport fails with `Protocol not supported`:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "/usr/bin/nexutil -k"
```

Observed (verbatim output):

```text
nex_init_netlink: socket error (93: Protocol not supported)
nex_init_netlink: socket error (93: Protocol not supported)
nex_init_netlink: bind error (9: Bad file descriptor)
nex_init_netlink: connect error (9: Bad file descriptor)
ERR (__nex_driver_netlink): no valid answer received
chanspec: 0x6863, 6g85/160
```

Similarly, in `stack=stock`:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "/usr/bin/nexutil --chanspec=44/20"
```

Observed:

```text
nex_init_netlink: socket error (93: Protocol not supported)
...
ERR (__nex_driver_netlink): no valid answer received
```

Interpretation:

- **Known:** the netlink protocol used by `nexutil` is not supported in `stack=stock`.
- **Unknown:** whether `nexutil` has other transports/modes when not in `stack=nexmon` (the final `chanspec:` line above may be stale/garbage; no valid answer was reported).

## Transport (from `strace`)

### Summary

`nexutil` communicates with the Nexmon scanner stack via **netlink**:

- `socket(AF_NETLINK, SOCK_RAW, 0x1f)` (protocol `0x1f` / `31`).
- One netlink socket is `connect()`ed to `{ nl_pid=0 }` (kernel).
- A second netlink socket is `bind()`ed to `{ nl_pid=<client-pid> }` for receiving replies.
- Receive socket uses `SO_RCVTIMEO_OLD` with a 16-byte value starting with `01 00 00 00` (interpreted as a 1-second timeout; inference).

Evidence (excerpt from `/tmp/dirtsim3-nexutil-set.trace`):

```text
socket(AF_NETLINK, SOCK_RAW, 0x1f /* NETLINK_??? */) = 3<NETLINK:[...]>
socket(AF_NETLINK, SOCK_RAW, 0x1f /* NETLINK_??? */) = 4<NETLINK:[...]>
setsockopt(4<NETLINK:[...]>, SOL_SOCKET, SO_RCVTIMEO_OLD, "\x01\x00\x00\x00...", 16) = 0
bind(4<NETLINK:[...]>, {sa_family=AF_NETLINK, nl_pid=<pid>, nl_groups=00000000}, 16) = 0
connect(3<NETLINK:[...]>, {sa_family=AF_NETLINK, nl_pid=0, nl_groups=00000000}, 16) = 0
```

## Netlink Message Framing

### Netlink header fields (observed via `strace`)

All observed requests use:

- `nlmsg_type=0`
- `nlmsg_flags=0`
- `nlmsg_seq=0`
- `nlmsg_pid=<sender pid>`

Example (set-channel send):

```text
sendto(..., [{nlmsg_len=48, nlmsg_type=0, nlmsg_flags=0, nlmsg_seq=0, nlmsg_pid=<pid>}, <payload>], 48, ...)
```

All observed replies arrive as `nlmsg_type=NLMSG_DONE` with `nlmsg_pid=0`.

### “NEX” payload structure

All observed `nexutil` requests share an 8-byte prefix:

```text
4e 45 58 00 00 00 00 00
N  E  X  \0 \0 \0 \0 \0
```

After this prefix, the payload contains 32-bit little-endian fields (endianness inference based on the `0xd02c` example below).

#### Common layout (inferred from multiple messages)

```text
offset  size  name            notes
0x00    8     magic           "NEX\0\0\0\0\0" (observed)
0x08    4     cmdId           varies by operation (observed)
0x0c    4     opOrFlag        0 for get, 1 for set (inferred from -k vs --chanspec and -m/-p)
0x10    ...   operation data  varies (observed)
```

## Channel Set: `--chanspec=44/20` (Observed On-Wire)

### Request bytes (payload)

From `/tmp/dirtsim3-nexutil-set.trace`:

```text
nlmsg_len = 48  (16-byte netlink header + 32-byte payload)
payload   = 4e 45 58 00 00 00 00 00  07 01 00 00  01 00 00 00
            63 68 61 6e 73 70 65 63 00  2c d0 00 00  00 00 00
```

Annotated:

```text
00: 4e 45 58 00 00 00 00 00   magic "NEX\0\0\0\0\0"
08: 07 01 00 00               cmdId = 0x00000107 (observed)
0c: 01 00 00 00               opOrFlag = 1 (inferred "set" from behavior)
10: 63 68 61 6e 73 70 65 63 00  ASCII "chanspec\0" (observed)
19: 2c d0 00 00               value = 0x0000d02c (little-endian) (observed)
1d: 00 00 00                  padding (observed)
```

### Response

The set-channel invocation receives a `NLMSG_DONE` message with `nlmsg_len=20` (i.e., 4 data bytes beyond the netlink header). `strace` printed the data as the integer `4932417` (`0x004b4341`).

- **Known:** the reply type is `NLMSG_DONE` and it is received after the send.
- **Inferred:** if the 4 data bytes are interpreted as little-endian ASCII, `0x004b4341` corresponds to `41 43 4b 00` = `"ACK\0"`.

## Channel Get: `-k` (Observed On-Wire)

### Request bytes (payload)

From `/tmp/dirtsim3-nexutil-get.trace`:

```text
nlmsg_len = 44  (16-byte netlink header + 28-byte payload)
payload   = 4e 45 58 00 00 00 00 00  06 01 00 00  00 00 00 00
            63 68 61 6e 73 70 65 63 00 00 00 00
```

Annotated:

```text
00: 4e 45 58 00 00 00 00 00   magic "NEX\0\0\0\0\0"
08: 06 01 00 00               cmdId = 0x00000106 (observed)
0c: 00 00 00 00               opOrFlag = 0 (inferred "get" from behavior)
10: 63 68 61 6e 73 70 65 63 00 00 00 00  ASCII "chanspec\0" + pad (observed)
```

### Response bytes (payload)

The reply is also `nlmsg_len=44` and `nlmsg_type=NLMSG_DONE`, and its payload is:

```text
payload = 4e 45 58 00 00 00 00 00  06 01 00 00  00 00 00 00
          2c d0 00 00  73 70 65 63 00 00 00 00
```

Annotated:

```text
00: 4e 45 58 00 00 00 00 00   magic "NEX\0\0\0\0\0" (echoed)
08: 06 01 00 00               cmdId echoed
0c: 00 00 00 00               opOrFlag echoed
10: 2c d0 00 00               value = 0x0000d02c (little-endian) (observed)
14: 73 70 65 63 00 00 00 00   ASCII "spec\0\0\0\0" (leftover bytes) (observed)
```

Notes:

- **Known:** the 4-byte value appears at offset `0x10` in the response payload.
- **Known:** `nexutil -k` printed `chanspec: 0xd02c, 44` for this response.
- **Unknown:** why the trailing bytes contain `"spec"`; the response payload appears to overwrite the first 4 bytes of the `"chanspec"` string with the 32-bit value and leaves the remainder.

## How `44/20` Appears “On the Wire”

In the set request for `--chanspec=44/20`, the 20 MHz channel selection is conveyed as:

- a variable name: ASCII `"chanspec\0"`
- followed by a 32-bit little-endian integer: `0x0000d02c` (bytes `2c d0 00 00`)

Separately, `nexutil -k` rendered that value as:

```text
chanspec: 0xd02c, 44
```

## 20 MHz Chanspec Encoding (2.4 GHz vs 5 GHz)

The current DirtSim scanner channel plan is built by `ScannerService::buildChannelPlan()` (see `apps/src/os-manager/network/ScannerService.cpp`) and includes:

- 2.4 GHz: channels 1–11.
- 5 GHz: channels 36, 40, 44, 48, 149, 153, 157, 161, 165.

This section documents black-box evidence (set → readback → `iw dev` confirm, with syscall traces) sufficient to support the 20 MHz chanspec encoding for **every channel in that plan**.

### Evidence capture pattern (per channel)

Precondition: `dirtsim-nexmon-mode status` reports `stack=nexmon`.

For each requested `CH/20`:

```bash
# Set
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-set-CH.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil --chanspec=CH/20"

# Read back + confirm
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  "sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-get-CH.trace -e trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl \
   /usr/bin/nexutil -k; iw dev"
```

#### Exact commands run (scan plan completion)

The earlier sections captured channels 1/6/11 and 36/44/165. The remaining channels in the scan plan were captured (while in `stack=nexmon`) using:

```bash
ssh -o UserKnownHostsFile=/tmp/garden_known_hosts -o StrictHostKeyChecking=no dirtsim3.local \
  'channels="2 3 4 5 7 8 9 10 40 48 149 153 157 161"; \
   trace_expr="trace=socket,bind,connect,sendto,sendmsg,recvfrom,recvmsg,setsockopt,getsockopt,ioctl"; \
   for ch in $channels; do \
     echo "CH $ch"; \
     sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-set-${ch}.trace -e $trace_expr \
       /usr/bin/nexutil --chanspec=${ch}/20 >/tmp/nexutil-set-${ch}.out 2>&1; \
     { \
       sudo strace -f -s 512 -xx -yy -o /tmp/nexutil-get-${ch}.trace -e $trace_expr \
         /usr/bin/nexutil -k; \
       iw dev; \
     } >/tmp/nexutil-get-${ch}.out 2>&1; \
   done'
```

### Summary table (requested vs observed)

All rows below were observed on `dirtsim3.local` while `stack=nexmon`.

| Requested channel | Requested string | `nexutil -k` reported chanspec | Low byte | `iw dev` confirmed channel |
|---:|:---:|:---:|:---:|---:|
| 1   | `1/20`   | `0x1001` | `0x01` | 1 |
| 2   | `2/20`   | `0x1002` | `0x02` | 2 |
| 3   | `3/20`   | `0x1003` | `0x03` | 3 |
| 4   | `4/20`   | `0x1004` | `0x04` | 4 |
| 5   | `5/20`   | `0x1005` | `0x05` | 5 |
| 6   | `6/20`   | `0x1006` | `0x06` | 6 |
| 7   | `7/20`   | `0x1007` | `0x07` | 7 |
| 8   | `8/20`   | `0x1008` | `0x08` | 8 |
| 9   | `9/20`   | `0x1009` | `0x09` | 9 |
| 10  | `10/20`  | `0x100a` | `0x0a` | 10 |
| 11  | `11/20`  | `0x100b` | `0x0b` | 11 |
| 36  | `36/20`  | `0xd024` | `0x24` | 36 |
| 40  | `40/20`  | `0xd028` | `0x28` | 40 |
| 44  | `44/20`  | `0xd02c` | `0x2c` | 44 |
| 48  | `48/20`  | `0xd030` | `0x30` | 48 |
| 149 | `149/20` | `0xd095` | `0x95` | 149 |
| 153 | `153/20` | `0xd099` | `0x99` | 153 |
| 157 | `157/20` | `0xd09d` | `0x9d` | 157 |
| 161 | `161/20` | `0xd0a1` | `0xa1` | 161 |
| 165 | `165/20` | `0xd0a5` | `0xa5` | 165 |

### On-wire payload bytes (per channel)

For each channel below, the **set request payload** comes from the `sendto(...)` payload in the captured `nexutil --chanspec=CH/20` strace logs, and the **get response payload** comes from the `recvfrom(...)` payload in the captured `nexutil -k` strace logs.

#### Channel 1 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 01 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 01 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 6 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 06 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 06 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 11 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 0b 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 0b 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 36 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 24 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 24 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 44 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 2c d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 2c d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 165 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 a5 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 a5 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 2 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 02 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 02 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 3 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 03 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 03 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 4 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 04 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 04 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 5 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 05 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 05 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 7 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 07 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 07 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 8 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 08 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 08 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 9 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 09 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 09 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 10 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 0a 10 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 0a 10 00 00 73 70 65 63 00 00 00 00
```

#### Channel 40 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 28 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 28 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 48 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 30 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 30 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 149 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 95 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 95 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 153 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 99 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 99 d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 157 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 9d d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 9d d0 00 00 73 70 65 63 00 00 00 00
```

#### Channel 161 / 20 MHz

```text
set request payload (32 bytes):
4e 45 58 00 00 00 00 00 07 01 00 00 01 00 00 00 63 68 61 6e 73 70 65 63 00 a1 d0 00 00 00 00 00
get response payload (28 bytes):
4e 45 58 00 00 00 00 00 06 01 00 00 00 00 00 00 a1 d0 00 00 73 70 65 63 00 00 00 00
```

### Conclusion (strictly bounded)

- **Known (supported by evidence above):**
  - The full current scan plan channels (1–11, 36/40/44/48/149/153/157/161/165) are now covered by black-box evidence (set request + get response + `iw dev` confirmation for each channel).
  - For 2.4 GHz 20 MHz channels 1–11, `nexutil -k` reports chanspec values `0x1001` through `0x100b`, and `iw dev` confirms channels 1–11.
  - For 5 GHz 20 MHz channels 36/40/44/48/149/153/157/161/165, `nexutil -k` reports chanspec values `0xd024`, `0xd028`, `0xd02c`, `0xd030`, `0xd095`, `0xd099`, `0xd09d`, `0xd0a1`, `0xd0a5`, and `iw dev` confirms those channels.
- **Inferred rule (now supported by scan plan channels 1–11, 36/40/44/48/149/153/157/161/165):**
  - For **20 MHz** operation on this device/stack, the 16-bit `chanspec` appears to encode the channel number in the low byte, and uses a distinct high byte per band:
    - 2.4 GHz examples: `chanspec = 0x10<<8 | channel` (e.g., `0x1006` for channel 6).
    - 5 GHz examples: `chanspec = 0xd0<<8 | channel` (e.g., `0xd024` for channel 36).
- **Unknown:** whether the same band-high-byte rule applies to other 2.4 GHz channels beyond 1–11, other 5 GHz channels beyond 36/40/44/48/149/153/157/161/165, and/or non-20 MHz bandwidths.

## Optional: Monitor / Promisc Commands (Observed On-Wire)

These commands also use the same netlink transport and the same `NEX` 8-byte prefix.

### `nexutil -m2` (monitor mode set to 2)

From `/tmp/dirtsim3-nexutil-m2.trace`:

```text
nlmsg_len = 36 (16-byte netlink header + 20-byte payload)
payload   = 4e 45 58 00 00 00 00 00  6c 00 00 00  01 00 00 00  02 00 00 00
```

- **Known:** payload contains `cmdId=0x0000006c`, `opOrFlag=1`, and a final 32-bit value `2`.
- **Inferred:** `cmdId` and the final word correspond to “monitor mode” and “mode value”, respectively, because the CLI option invoked was `-m2`.

### `nexutil -p1` (promisc mode set to 1)

From `/tmp/dirtsim3-nexutil-p1.trace`:

```text
nlmsg_len = 36
payload   = 4e 45 58 00 00 00 00 00  0a 00 00 00  01 00 00 00  01 00 00 00
```

- **Known:** payload contains `cmdId=0x0000000a`, `opOrFlag=1`, and a final 32-bit value `1`.
- **Inferred:** `cmdId` and the final word correspond to “promisc mode” and “mode value”, respectively, because the CLI option invoked was `-p1`.

## Timeout / Error Behavior

### Timeout behavior

- **Known:** `nexutil` sets `SO_RCVTIMEO_OLD` on its receiving netlink socket with a 16-byte value beginning with `01 00 00 00` and otherwise zero.
- **Inferred:** this corresponds to a 1-second receive timeout (typical `struct timeval { tv_sec=1; tv_usec=0; }`), but the timeout failure path was not exercised/observed here.

### Errors when stack is not Nexmon

- **Known:** in `stack=stock`, attempts to open the netlink sockets fail with `Protocol not supported (93)` and `nexutil` reports `ERR (__nex_driver_netlink): no valid answer received`.

## Known vs Inferred vs Unknown (Summary)

### Known (directly observed)

- Transport is netlink: `socket(AF_NETLINK, SOCK_RAW, 0x1f)` with a connect-to-kernel + bound-recv socket pattern.
- Requests use a payload starting with `4e 45 58 00 00 00 00 00`.
- Channel set (`--chanspec=CH/20`) request payload includes ASCII `"chanspec\0"` and a 32-bit little-endian value; observed examples include:
  - 2.4 GHz scan plan: `0x1001` through `0x100b` (channels 1–11).
  - 5 GHz scan plan: `0xd024`, `0xd028`, `0xd02c`, `0xd030`, `0xd095`, `0xd099`, `0xd09d`, `0xd0a1`, `0xd0a5` (channels 36/40/44/48/149/153/157/161/165).
- Channel get (`-k`) response includes the 32-bit value at payload offset `0x10`.
- In `stack=stock`, `nexutil`’s netlink socket creation fails with `Protocol not supported (93)`.

### Inferred (supported but not proven)

- Payload integers are little-endian (fits `2c d0 00 00` → `0xd02c`).
- `opOrFlag` at offset `0x0c` behaves like `0=get` / `1=set`.
- The 4-byte `NLMSG_DONE` reply for set-like operations is `"ACK\0"` (inferred from `4932417 == 0x004b4341`).
- For the observed **20 MHz** cases, the channel number is the low 8 bits of the `chanspec` value, and the high byte differs between 2.4 GHz (`0x10`) and 5 GHz (`0xd0`).

### Unknown / Not established

- Whether `cmdId` values map to stable public identifiers (and if so, what the complete set is).
- Whether the 20 MHz `chanspec` patterns observed here generalize beyond the current scan plan channels, or to non-20 MHz bandwidths.
- Full semantics of the trailing `"spec"` bytes in the get response (beyond being present).
- Robust timeout/retry/error handling requirements beyond the observed 1-second socket receive timeout configuration.
