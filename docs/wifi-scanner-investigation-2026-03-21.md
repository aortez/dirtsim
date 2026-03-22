# Wi-Fi Scanner Investigation Notes

Working notes for the `scanner-viz` branch investigation on March 21, 2026.

## Summary

The scanner-mode refactor appears to work when the Raspberry Pi Wi-Fi stack is healthy.

The main investigation risk is misdiagnosing a broken Pi-side Wi-Fi state as an app bug.
We already saw a failure mode that disappeared after rebooting `dirtsim3.local` with the
same app build and the same `onionchan` configuration. That means we should be very
careful about adding product logic that tries to "handle" this broken state.

The current policy should be:

1. Treat reboot-sensitive Wi-Fi failures as system diagnostics first.
2. Only change product code when the behavior is reproducible in a healthy system or
   when the code is clearly wrong independent of system state.
3. Prefer better logging and evidence collection over speculative retry logic.

## Update (March 22, 2026): Unrecoverable "bad state" root cause

The unrecoverable state where `dirtsim3.local` stops accepting new SSH connections
(sessions hang during SSH banner exchange until reboot) was reproduced and traced to a
kernel Oops during Wi-Fi module unload while entering scanner mode:

- Trigger: `modprobe -r brcmfmac_wcc` while `wpa_supplicant` is running and a
  `P2P-device` wdev exists.
- Effect: kernel Oops in `cfg80211` / `brcmfmac_wcc` unload path, after which the host
  becomes unstable until reboot.
- Mitigation: stop `wpa_supplicant`, wait until the `P2P-device` disappears (`iw dev`),
  then bring `wlan0` down before unloading `brcmfmac_wcc` / `brcmfmac`.

With that mitigation in `dirtsim-nexmon-mode`, `canExerciseWifiAndScannerBackendOnly`
passes without triggering the kernel Oops.

## What We Know

### Scanner mode itself is not the main current blocker.

- `canExerciseWifiAndScannerBackendOnly` passed on `dirtsim3.local` after reboot.
- `canExerciseWifiAndScanner` also passed in the single-radio `onionchan` setup after
  the UI/test-harness fixes landed.
- That means scanner enter, scanner capture, scanner exit, and Wi-Fi restore can all
  work end to end on the current branch.

### `onionchan` behavior is Pi-sensitive and state-sensitive.

- This host can connect to `onionchan` while `dirtsim3.local` is failing.
- With one `onionchan` radio enabled, the Pi could pass both backend and UI tests.
- With both `onionchan` radios enabled, failures became intermittent.
- Rebooting `dirtsim3.local` with both radios enabled made the same backend-only test
  pass temporarily.

This strongly suggests a Pi-side Wi-Fi state problem, or a Pi/AP interaction problem,
not a clean deterministic scanner regression.

### Some app-side issues were real, but they were not the same as the broken Pi state.

- The UI functional test had a real race around `NetworkWifiConnecting`.
- The UI state machine could lag the actual network panel screen.
- The backend/UI functional harness needed better cleanup and connect-result handling.

Those were legitimate fixes because they reproduced separately and were not dependent on
the Pi being in a broken Wi-Fi state.

### The latest diagnostic run shows a broken Wi-Fi attempt clearly.

After adding more logging to `WifiManagerLibNm.cpp`, a backend-only run on
March 21, 2026 showed:

- request start with `visible_aps=none` for `onionchan`
- one saved `onionchan` profile still existed
- NetworkManager accepted the connect request anyway
- device transitions: `deactivating -> disconnected -> prepare -> config -> need-auth -> prepare -> config`
- then the device sat in association for about `25.6s`
- final failure: `WiFi activation timed out in association stage after 25s (state=config, reason=none)`
- a few seconds later, `onionchan` was visible again in the snapshot
- the Pi ended healthy on `turtleback`

That pattern looks like a bad system state, not a simple timeout constant issue.

### The strongest evidence so far is a paired run with no code changes in between.

On March 21, 2026, with the same build, same Pi, same two-radio `onionchan`
configuration, and no reboot between the two test runs:

- the first backend-only run after reboot passed in about `37.4s`
- the second backend-only run, started shortly after without rebooting, failed in
  about `97.1s`

What changed between the two runs:

- In the passing post-reboot run, `wpa_supplicant` associated with
  `24:f5:a2:07:48:2e`, completed WPA key negotiation, and connected.
- In the later failing run, `wpa_supplicant` first timed out authenticating with
  `24:f5:a2:07:48:2e`, then tried `24:f5:a2:07:48:2f`, then disconnected.
- On retry in that same later run, it timed out again on `2e`, then fell back to
  `2f`, and NetworkManager eventually surfaced `association took too long`.
- The final user-visible error from os-manager became
  `WiFi activation failed (state=failed, reason=no-secrets)`, but the logs show that
  this happened after the association/authentication failures, not before them.
- Cleanup restore to `turtleback` then also stalled in the association stage, even
  though the Pi later recovered and ended healthy on `turtleback`.

This is the clearest proof yet that we are dealing with a reboot-sensitive Wi-Fi stack
state on the Pi, or a Pi/AP interaction state, rather than a deterministic product
logic regression.

### A simple raw `nmcli` network-switch test did not reproduce the failure immediately.

After reboot, a plain NetworkManager-only sequence succeeded with Wi-Fi power save still
enabled:

- connect to `onionchan`
- connect to `turtleback`
- bring `onionchan` back up
- bring `turtleback` back up

That sequence succeeded under raw `nmcli` without using dirtsim APIs.

This is important because it means the broad claim "simple SSID switching is enough to
break the Pi" is not yet supported. The next useful system-side test needs to look more
like the app path, especially repeated create / activate / delete churn for ephemeral
profiles, rather than a single short switch cycle.

### A heavier raw `nmcli` create / activate / delete churn test also succeeded.

On March 21, 2026, after reboot, a system-only diagnostic harness ran six full
alternations between `onionchan` and `turtleback` using raw `nmcli`:

- each hop used `nmcli device wifi connect ...`
- per-cycle connection profiles were created with new names
- older cycle profiles were deleted as the test progressed to mimic ephemeral-profile
  churn more closely
- Wi-Fi power save remained enabled for the whole run

All twelve connection hops succeeded:

- `connect_onionchan_cycle_1` through `connect_onionchan_cycle_6`
- `connect_turtleback_cycle_1` through `connect_turtleback_cycle_6`

The Pi finished healthy on `diag-turtleback-6`, with both `onionchan` radios still
visible and power save still reported as `on`.

This matters because it means even a much closer system-only approximation of the app's
profile churn still does not reproduce the broken state. That pushes the likely trigger
back toward dirtsim's exact libnm / orchestration path, or toward some interaction that
the raw `nmcli` harness is still not exercising.

### A direct `dirtsim-cli network` churn test also succeeded twice in a row.

On March 21, 2026, a second diagnostic harness exercised the Pi through
`dirtsim-cli network connect`, which uses `WifiManagerLibNm` directly but avoids the
os-manager WebSocket layer, UI layer, scanner mode, and functional-test harness.

Two six-cycle runs succeeded:

- a post-reboot run from `10:35:26Z` to `10:36:46Z`
- an immediate no-reboot run from `10:36:52Z` to `10:37:55Z`

Each run alternated:

- `onionchan` connect with password
- `turtleback` connect with password

All twelve connection hops in each run succeeded.

This is an important narrowing result:

- raw `nmcli` create / activate / delete churn succeeds
- direct `WifiManagerLibNm` churn via `dirtsim-cli network` also succeeds
- the earlier flaky failure still appears specific to the os-manager / backend-test path,
  or to long-lived process state that these one-shot CLI runs do not retain

### A stable-control-plane backend-only pair reproduced the failure again.

On March 21, 2026, the backend-only functional test was rerun over the Pi's stable
`eth1` address (`192.168.1.142`) so the control path stayed on Ethernet while `wlan0`
was exercised.

With the same build and same AP configuration:

- `run1_post_reboot` passed in `33.966s`
- `run2_no_reboot` failed in `99.512s`

The failure was:

- `WiFi connect failed for onionchan: WiFi activation timed out in association stage after 25s (state=config, reason=none); cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: WiFi activation timed out in association stage after 25s (state=config, reason=none)`

This is important because it reproduces the post-reboot pass / no-reboot fail pattern
again even when host discovery noise is removed from the harness.

### The canceled-connect path matters, but it is not sufficient on its own.

The lower-level success cases did not exercise mid-connect cancellation:

- raw `nmcli` churn: no cancel step
- direct `dirtsim-cli network` churn: no cancel step

The backend-only test does exercise cancellation, and in the failing second run the
sequence was:

- request `#7`: start `onionchan` connect, then cancel it as part of the test
- request `#8`: start the real `onionchan` connect after cancellation
- request `#8` then stalled twice in association and timed out
- cleanup request `#9` to restore `turtleback` also stalled in association and timed out

Supporting `wpa_supplicant` evidence from the failing run:

- `10:46:31`: tried to associate with `24:f5:a2:07:48:2e`
- `10:46:41`: authentication with `2e` timed out
- `10:47:04`: tried `2e` again
- `10:47:14`: authentication with `2e` timed out again
- `10:47:18`: tried `24:f5:a2:07:48:2f`
- `10:47:27`: disconnected without success

That made the cancel path look like the strongest suspect, but a follow-up isolated test
changed the picture again.

On March 21, 2026, a new backend-only cancel/reconnect test exercised:

- baseline connect
- start cancel-target connect
- wait until cancelable
- cancel connect
- reconnect cancel target
- reconnect baseline

It deliberately did **not** enter scanner mode.

Using the same stable `eth1` control path and the same reboot / no-reboot pairing:

- `run1_post_reboot` passed
- `run2_no_reboot` also passed

So the cancel path is **not sufficient by itself** to reproduce the failure.

The current inference is narrower:

- plain connect churn does not reproduce
- direct `WifiManagerLibNm` churn does not reproduce
- backend cancel/reconnect without scanner does not reproduce
- the full backend-only test still reproduces post-reboot pass / no-reboot fail

That pushes suspicion toward an interaction involving scanner mode, or state left behind
by the first full backend-only run that is not present in the isolated cancel-only run.

### The contrast repeated on a second confirmation round.

Later on March 21, 2026, both paired tests were rerun again from fresh reboots.

The full backend-only Wi-Fi/scanner pair repeated the same pattern:

- `run1_post_reboot` passed
- `run2_no_reboot` failed again

The repeated full-pair failure bundle is:

- `/tmp/dirtsim-full-backend-pair-20260321T164359Z`

The cancel-only backend pair also repeated cleanly:

- `run1_post_reboot` passed
- `run2_no_reboot` passed

The repeated cancel-only bundle is:

- `/tmp/dirtsim-cancel-backend-pair-20260321T164713Z`

That makes the contrast materially stronger:

- full backend-only test: reproducible pass then fail
- cancel-only backend test: reproducible pass then pass

So the remaining suspect is not just "backend cancel logic." It is something present in
the full backend-only path and absent from the cancel-only path, with scanner mode now
the most obvious remaining difference.

### Scanner mode alone also passed twice in a row.

To isolate scanner mode directly, a new backend-only test was added:

- connect baseline Wi-Fi
- enter scanner mode
- verify scanner snapshot
- exit scanner mode
- verify Wi-Fi restore

It deliberately does **not** exercise the canceled-connect sequence.

On March 21, 2026, using the same stable `eth1` control path and the same
reboot / no-reboot pairing:

- `run1_post_reboot` passed in `26.053s`
- `run2_no_reboot` also passed in `25.506s`

The paired bundle is:

- `/tmp/dirtsim-scanner-backend-pair-20260321T165544Z`

Post-test state in both runs was healthy:

- `scanner_mode_active=false`
- `wlan0` restored to `turtleback`
- both `onionchan` radios still visible

This matters because it removes scanner enter / capture / exit / restore as a
standalone trigger. The remaining reproducible difference in the failing full
backend-only path is now the combination of:

- canceled connect attempt
- later real connect attempt
- scanner mode sequence

In other words, cancel alone is not sufficient, scanner mode alone is not sufficient,
but the full combined path still is.

### Cancel plus scanner still passed twice.

To check whether the cancel event itself was the missing ingredient, another
backend-only split was added:

- baseline connect
- start `onionchan` connect
- wait until cancelable
- cancel it
- reconnect baseline
- enter scanner mode
- verify snapshot
- exit scanner mode
- verify restore

This path deliberately never completes the real switch to `onionchan`.

On March 21, 2026, with the same reboot / no-reboot pairing:

- `run1_post_reboot` passed in `43.060s`
- `run2_no_reboot` also passed in `42.144s`

The paired bundle is:

- `/tmp/dirtsim-cancel-then-scanner-backend-pair-20260321T171057Z`

That means the cancel event is also **not sufficient**, even when followed by
scanner mode.

### Real switch plus scanner reproduced the failure without any cancel.

The next split removed cancellation entirely and kept only:

- baseline connect
- real `onionchan` connect
- reconnect baseline
- enter scanner mode
- verify snapshot
- exit scanner mode
- verify restore

On March 21, 2026, with the same reboot / no-reboot pairing:

- `run1_post_reboot` passed in `38.780s`
- `run2_no_reboot` failed in `78.314s`

The paired bundle is:

- `/tmp/dirtsim-switch-then-scanner-backend-pair-20260321T171254Z`

The second run failed before scanner mode even started:

- `WiFi connect failed for onionchan: WiFi activation failed (state=failed, reason=no-secrets)`

Supporting evidence from the failing second run:

- request `#6` again preferred `24:F5:A2:07:48:2E` at `5200 MHz`
- `wpa_supplicant` tried to associate with `2e` at `17:14:15`
- authentication timed out at `17:14:25`
- retry attempt 2 again tried `2e` at `17:14:45`
- authentication timed out again at `17:14:55`
- NetworkManager / libnm then surfaced `failed/no-secrets`
- cleanup restore to `turtleback` succeeded afterward

This is the strongest narrowing result so far:

- cancel is not required
- scanner mode in the failing run is not required
- but a prior run that includes a real `onionchan` switch and scanner mode is enough
  to prime the next `onionchan` failure

### Real switch without scanner also passed twice.

To make sure scanner mode is actually part of the trigger, one more backend-only
control test was added:

- baseline connect
- real `onionchan` connect
- reconnect baseline

No cancel and no scanner mode.

On March 21, 2026, with the same reboot / no-reboot pairing:

- `run1_post_reboot` passed in `18.491s`
- `run2_no_reboot` also passed in `20.996s`

The paired bundle is:

- `/tmp/dirtsim-switch-backend-pair-20260321T171810Z`

Post-test state remained healthy:

- `scanner_mode_active=false`
- `wlan0` restored to `turtleback`
- `onionchan` remained saved and visible

That means scanner mode is part of the reproducer after all, but only in combination
with a completed real switch to `onionchan`.

### Forgetting `onionchan` before scanner exit did not remove the failure.

To check whether the exit-side problem was only caused by saved-profile auto-activation,
another backend-only variant was added:

- baseline connect
- forget `onionchan`
- real `onionchan` connect
- reconnect baseline
- forget `onionchan` again so it is not saved before scanner mode
- enter scanner mode
- verify snapshot
- exit scanner mode
- verify restore

On March 21, 2026, with the same reboot / no-reboot pairing:

- `run1_post_reboot` passed
- `run2_no_reboot` failed

The paired bundle is:

- `/tmp/dirtsim-switch-forget-then-scanner-backend-pair-20260321T175858Z`

The failing second run is important because request `#6` had:

- `saved profile candidates: none`

and still failed with:

- `WiFi connect failed for onionchan: WiFi activation timed out in association stage after 25s (state=config, reason=none); cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: WiFi activation failed (state=failed, reason=ssid-not-found)`

That means the earlier auto-activation-of-`onionchan` observation may still matter, but
it is **not sufficient** to explain the whole reproducer. Preventing `onionchan` from
remaining saved before scanner exit did not eliminate the later failure.

### The first successful switch-then-scanner run shows an exit-side auto-activation race.

Looking closely at the **passing** `run1_post_reboot` logs from
`/tmp/dirtsim-switch-then-scanner-backend-pair-20260321T171254Z` shows a concrete
restore hazard after scanner exit:

- scanner exit returned control to NetworkManager around `17:13:50`
- NetworkManager recreated `wlan0` / `p2p-dev-wlan0` and started active scanning
- at `17:13:54`, NetworkManager policy auto-activated saved profile `onionchan`
- only after that did os-manager explicitly activate `turtleback`
- NetworkManager then interrupted the in-flight `onionchan` activation with a new
  activation for `turtleback`

So even the "passing" first run contains a hidden extra sequence:

- completed real switch to `onionchan`
- scanner mode round-trip
- partial auto-activation of `onionchan` during exit restore
- forced interruption back to `turtleback`

That is a much more concrete candidate trigger for the later broken state than the
earlier broader hypothesis of "scanner mode is bad somehow."

### A first timing-only fix attempt made the Pi wedge harder and was reverted.

A local experiment tried two minimal changes in `OperatingSystemManager.cpp`:

- remove the extra `requestScan()` immediately after scanner exit
- remove the initial `2s` sleep before the first restore attempt

The intent was to beat NetworkManager's auto-activation window. In practice, the first
retest run on `dirtsim3.local` became worse:

- the Pi stayed pingable on `192.168.1.142`
- TCP port `22` stayed open
- but SSH stopped sending a banner while the first retest run hung

That change was reverted locally and should **not** be treated as a validated fix.
The race hypothesis still looks plausible, but this particular timing tweak is not a
safe solution.

### `NetworkManager` restart alone did not recover the bad state, but restarting `wpa_supplicant` did.

After another fresh reboot on March 21, 2026, the switch-then-scanner reproducer was
run again to establish a recovery baseline:

- `run1_post_reboot` passed
- `run2_no_reboot` failed again

The paired bundle is:

- `/tmp/dirtsim-switch-then-scanner-backend-service-recovery-pair-20260321T180556Z`

The failing second run ended with:

- `WiFi connect failed for onionchan: WiFi activation timed out in association stage after 25s (state=config, reason=none); cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: WiFi activation failed (state=disconnected, reason=connection-removed)`

From that failed state, `NetworkManager` was restarted without rebooting the Pi, and the
same backend test was rerun:

- `/tmp/dirtsim-switch-then-scanner-after-nm-restart-20260321T181356Z`

That run still failed:

- `WiFi connect failed for onionchan: WiFi activation failed (state=failed, reason=no-secrets); cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: WiFi activation timed out in association stage after 25s (state=config, reason=none)`

An immediate second run after that, still without rebooting and still without restarting
`wpa_supplicant`, also failed:

- `/tmp/dirtsim-switch-then-scanner-after-nm-restart-then-no-restart-20260321T181657Z`
- `WiFi connect failed for onionchan: WiFi activation failed (state=failed, reason=no-secrets)`

Restarting both `wpa_supplicant` and `NetworkManager` together did recover once:

- `/tmp/dirtsim-switch-then-scanner-after-wpa-nm-restart-20260321T182019Z`
- success in `40968ms`

That was already a strong narrowing result:

- a full reboot is **not strictly required** to recover
- `NetworkManager` policy/process state alone is **not sufficient** to clear the issue
- restarting `wpa_supplicant` looked like the likely effective step

A later, smaller-boundary check confirmed that `wpa_supplicant` alone is enough.

From a healthy `turtleback` baseline on March 21, 2026, the switch-then-scanner test
was run until it failed again:

- `/tmp/dirtsim-wpa-only-recovery-20260321T184840Z/attempt1`
- failure in `102772ms`
- `WiFi connect failed for onionchan: WiFi activation failed (state=failed, reason=no-secrets)`

At that exact failed state, supplicant-focused snapshots were captured:

- `wpa_cli -i wlan0 status`
- `wpa_cli -i wlan0 list_networks`
- `wpa_cli -i wlan0 scan_results`
- `iw dev wlan0 link`
- os-manager, NetworkManager, `wpa_supplicant`, and kernel journals

Then only `wpa_supplicant` was restarted, leaving `NetworkManager` running, and the
same backend test was rerun immediately:

- `/tmp/dirtsim-wpa-only-recovery-20260321T184840Z/after_wpa_only_restart`
- success in `40061ms`

This is the cleanest recovery-boundary result so far:

- reboot is **not** required
- `NetworkManager` restart alone is **not** enough
- `wpa_supplicant` restart alone **is** enough, at least in this reproduced case

That pushes suspicion below plain `NetworkManager` policy state and very strongly
toward either:

- `wpa_supplicant` internal state
- `wpa_supplicant` / driver interaction state
- or a lower Wi-Fi stack state that happens to get cleared when `wpa_supplicant` is restarted

### `nmcli radio wifi off/on` did not recover the state, and it left the same supplicant process running.

To separate "supplicant process state" from "basic radio/device reset," another
recovery-boundary test was run on March 21, 2026:

- start from a healthy-looking `turtleback` baseline
- run `canSwitchThenScannerBackendOnly` until it fails
- toggle Wi-Fi with `nmcli radio wifi off` / `on`
- rerun the same backend test immediately

The bundle is:

- `/tmp/dirtsim-radio-toggle-recovery-20260321T191630Z`

The first run failed as expected:

- `/tmp/dirtsim-radio-toggle-recovery-20260321T191630Z/attempt1`
- failure in `102965ms`
- `WiFi connect failed for onionchan: WiFi activation timed out in association stage after 25s (state=config, reason=none)`

After the radio toggle, the immediate rerun still failed:

- `/tmp/dirtsim-radio-toggle-recovery-20260321T191630Z/after_radio_toggle`
- failure in `98194ms`
- `WiFi connect failed for onionchan: WiFi activation timed out in association stage after 25s (state=config, reason=none); cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: WiFi activation failed (state=failed, reason=ssid-not-found)`

Two details make this especially useful:

- the radio toggle left `wlan0` in `connecting (getting IP configuration): turtleback`, not a fully fresh Wi-Fi state
- the `wpa_supplicant` PID stayed the same across both runs: `1993788`

The post-toggle `wpa_supplicant` log still showed the same kind of behavior:

- repeated association attempts against `2e` / `2f`
- authentication timeouts
- BSSID ignore-list increments
- `ioctl[SIOCSIWSCAN]: Resource temporarily unavailable`
- later `Reject scan trigger since one is already pending`

That means a basic Wi-Fi radio reset is **not equivalent** to restarting
`wpa_supplicant`. The current evidence now points more strongly at state held inside
the running supplicant process, or state tightly coupled to it, rather than a simple
radio/device condition that `nmcli radio wifi off/on` can clear.

### `SIGHUP` to `wpa_supplicant` also recovered, but only by changing the PID.

One more smaller-boundary check was attempted after the radio-toggle result:

- reproduce the failure again
- send `SIGHUP` to the running `wpa_supplicant` PID
- rerun the same backend test immediately

The initial attempt hit the failure again:

- `/tmp/dirtsim-wpa-hup-recovery-20260321T192147Z` (first failed run recorded separately in that sequence)

The `SIGHUP` state file is:

- `/tmp/dirtsim-wpa-hup-recovery-20260321T192147Z/hup_state.txt`

It showed:

- `before=1993788`
- `after=3531422`

So on this system, `SIGHUP` does **not** behave like a pure in-process reload. It
causes the `wpa_supplicant` PID to change, which makes it effectively another restart
path.

The immediate post-HUP backend test then passed:

- `/tmp/dirtsim-wpa-hup-recovery-20260321T192147Z/after_hup_functional_test.txt`
- success in `51721ms`

This does **not** reduce the recovery boundary below restart. It only confirms that:

- the useful reset is tied to replacing the running supplicant process
- `SIGHUP` is just another way to trigger that replacement on this box

### The service is minimal, has no `ExecReload`, and does not expose a `wpa_cli` control socket.

On `dirtsim3`, the service definition is:

- `ExecStart=/usr/sbin/wpa_supplicant -u`
- `Type=dbus`
- `Restart=no`
- no `ExecReload`

That is why `systemctl` reports no reload action, and it is also consistent with the
observed `SIGHUP` behavior:

- `CTRL-EVENT-TERMINATING`
- systemd deactivates the service
- systemd starts a new `wpa_supplicant` PID

This box also does not expose a usable `wpa_cli` control socket:

- `/run/wpa_supplicant` is absent
- `wpa_cli -i wlan0 ...` fails with `No such file or directory`

So there is no smaller `wpa_cli reconfigure` / `reassociate` test available without
changing the service launch configuration.

### A temporary `-dd -t` debug run shows the stale supplicant state much more clearly.

For one controlled fail/pass pair, a runtime-only override changed the service to:

- `ExecStart=/usr/sbin/wpa_supplicant -u -dd -t`

That override was applied temporarily, used for one pair, and then removed. The debug
pair bundle is:

- `/tmp/dirtsim-wpa-debug-pair-20260321T193145Z`

The outcome under debug logging was:

- `attempt1`: passed in `45995ms`
- `attempt2`: failed in `95048ms`
- `after_restart`: passed in `49444ms`

The restart state file is:

- `/tmp/dirtsim-wpa-debug-pair-20260321T193145Z/restart_state.txt`

It showed the expected process replacement:

- `before=3851000`
- `after=3991719`

The failing `attempt2` debug log makes the stale supplicant state much more explicit:

- initial selection was `24:f5:a2:07:48:2f`
- authentication to `2f` timed out
- `2f` was added to the ignore list and its ignore count was incremented
- `CTRL-EVENT-SCAN-FAILED ret=-1 retry=1` followed
- WPS bookkeeping still enumerated `2f` and `2e`, with `2f` marked `bssid_ignore=2`
- selection then fell back to `24:f5:a2:07:48:2e`
- later, after more churn, `2f` was selected again and timed out again
- NetworkManager eventually converted that association failure into `need-auth -> failed (reason 'no-secrets')`

The successful `after_restart` debug log looks different in exactly the way we would
expect from a fresh process:

- the new PID started with both `onionchan` BSSIDs at `bssid_ignore=0`
- it selected `24:f5:a2:07:48:2f`
- associated immediately
- completed WPA key negotiation immediately
- restored `turtleback` successfully afterward

This debug pair strengthens the current interpretation:

- the failure state is not just "the radio is bad"
- it is not just "NetworkManager policy got confused"
- the running `wpa_supplicant` process is accumulating state that affects BSSID choice
  and scan/association behavior across runs
- replacing that process clears the bad state

### One observed failure mode is stale address discovery, not total Pi loss.

After the second successful `dirtsim-cli network` churn run, the harness briefly failed
to collect logs because `dirtsim3.local` resolved to stale address `192.168.1.108`,
which was unreachable from this host.

The Pi itself remained reachable directly at `192.168.1.142` on `eth1`, while `wlan0`
carried the SSID-under-test address. During these experiments we observed:

- `eth1`: stable control-plane address `192.168.1.142`
- `wlan0` on `turtleback`: `192.168.77.208`
- `wlan0` on `onionchan`: `192.168.1.108`

That means `dirtsim3.local` can resolve to the Wi-Fi address under test, which changes
by SSID, while SSH to `192.168.1.142` remains stable.

When checked over `eth1`, the Pi remained healthy, with:

- `wlan0` connected to `turtleback`
- both `onionchan` radios still visible
- Wi-Fi power save still `on`

This means some apparent post-test "host unreachable" events are actually name / address
discovery instability, not proof that the Pi lost Wi-Fi completely.

## What We Should Not Conclude

- We should not conclude that the scanner refactor broke Wi-Fi in general.
- We should not conclude that increasing timeouts is the real fix.
- We should not conclude that app retry logic should be expanded to paper over a
  reboot-sensitive Pi Wi-Fi failure.
- We should not conclude that `onionchan` is globally broken, because it works from
  this host and sometimes works from the Pi after reboot.

## Guardrails

Before changing product Wi-Fi logic again, ask:

1. Does the failure survive a Pi reboot with the same build and same AP config?
2. Does it reproduce on a healthy system without depending on a previously broken state?
3. Do logs show a clear app logic bug, or only that NetworkManager / supplicant /
   driver state is unhealthy?

If the answer is "this goes away after reboot" or "the system itself is in a bad state,"
do diagnostics first and avoid turning that state into product behavior.

## Useful Test Commands

Fast deploy:

```bash
./update.sh --target dirtsim3.local --fast
```

Backend-only Wi-Fi/scanner test:

```bash
ssh dirtsim3.local \
  "dirtsim-cli functional-test canExerciseWifiAndScannerBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Stable control-plane variant for `dirtsim3`:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canExerciseWifiAndScannerBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Backend-only cancel/reconnect test:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canCancelWifiConnectBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Backend-only cancel-then-scanner test:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canCancelThenScannerBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Backend-only scanner-only test:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canExerciseScannerModeBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Backend-only switch-only test:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canSwitchWifiNetworksBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Backend-only switch-then-scanner test:

```bash
ssh dirtsim@192.168.1.142 \
  "dirtsim-cli functional-test canSwitchThenScannerBackendOnly \
   --wifi-config /tmp/wifi-functional-test.json"
```

Full UI-level Wi-Fi/scanner test:

```bash
ssh dirtsim3.local \
  "dirtsim-cli functional-test canExerciseWifiAndScanner \
   --wifi-config /tmp/wifi-functional-test.json"
```

Current network snapshot:

```bash
ssh dirtsim3.local "dirtsim-cli os-manager NetworkSnapshotGet"
```

Stable control-plane variant for `dirtsim3`:

```bash
ssh dirtsim@192.168.1.142 "dirtsim-cli os-manager NetworkSnapshotGet"
```

Relevant os-manager logs:

```bash
ssh dirtsim3.local \
  "sudo journalctl -u dirtsim-os-manager.service --since '2026-03-21 09:58:15' --no-pager"
```

## Current Diagnostic Instrumentation

`apps/src/core/network/WifiManagerLibNm.cpp` now logs:

- a unique connect request ID
- connect mode: saved profile vs ephemeral profile
- visible BSSIDs for the target SSID at request start
- saved connection candidates and UUIDs
- chosen target AP / BSSID
- every NetworkManager device state transition with elapsed timing
- explicit association-stage and overall-timeout stall messages

This instrumentation is intended to explain failures, not to mask them.

### A raw root-level switch-plus-scanner pair reproduces without dirtsim.

On March 21, 2026, the same high-level sequence was reproduced directly on
`dirtsim3.local` over the stable `eth1` control path using only:

- `nmcli`
- `/usr/bin/dirtsim-nexmon-mode`
- `/usr/bin/nexutil`
- `ip link`

No dirtsim binaries, libnm wrappers, os-manager, UI, or functional-test code were in
the control path for this repro.

The raw sequence was:

- connect `turtleback`
- delete saved `onionchan`
- connect `onionchan`
- reconnect `turtleback`
- enter scanner mode with the helper and `nexutil`
- exit scanner mode
- reconnect `turtleback`
- repeat the whole cycle without rebooting

The paired bundle is:

- `/tmp/system-only-switch-scanner-sudo-pair-20260321T194922`

Results:

- `run1` completed successfully end to end
- `run2` failed at the raw `onionchan` connect step before the second scanner round
  trip had started:
  - `nmcli -w 75 device wifi connect onionchan password ... ifname wlan0 name onionchan`
  - `Error: Connection activation failed: Secrets were required, but not provided.`

The rest of `run2` was allowed to continue so the box could recover to `turtleback`,
but the important boundary change is already clear:

- dirtsim is **not required** to reproduce the poisoned follow-on `onionchan` connect
- the failure can be induced below the app with raw NetworkManager plus the scanner
  helper path

### A clean raw switch-only control passed the meaningful steps twice.

After resetting the Pi with `wpa_supplicant` restart, a raw root-level control test
ran the same Wi-Fi switch sequence **without scanner mode**:

- connect `turtleback`
- delete saved `onionchan`
- connect `onionchan`
- reconnect `turtleback`
- repeat without rebooting

The clean control bundle is:

- `/tmp/system-only-switch-only-clean-sudo-pair-20260321T195405`

The meaningful result is:

- both `onionchan` connects succeeded
- both `turtleback` restores succeeded
- the only non-success in `run1` was the initial delete step reporting that there was
  no existing `onionchan` profile yet, which is not a Wi-Fi failure

So at the raw root-level boundary:

- switch-only still passes
- switch-plus-scanner reproduces the bad second `onionchan` connect

That matches the earlier app-level split and makes the combined trigger look real below
dirtsim as well.

### A clean raw scanner-only control also passed twice.

After the same clean baseline, a raw root-level scanner-only control ran:

- connect `turtleback`
- disconnect / unmanaged
- helper enable
- `nexutil -m2`
- `nexutil -p1`
- helper disable / managed yes / rescan
- reconnect `turtleback`
- repeat without rebooting

The paired bundle is:

- `/tmp/system-only-scanner-only-clean-sudo-pair-20260321T195559`

Results:

- `run1` passed with every step marked successful
- `run2` also passed with every step marked successful

So scanner enter / exit / restore alone still does **not** reproduce at the raw
root-level boundary.

### A dirty raw switch-only control immediately after the reproducer also started broken.

Before the clean control above, a raw switch-only control was started **immediately
after** the raw switch-plus-scanner reproducer, without first resetting
`wpa_supplicant`.

That bundle is:

- `/tmp/system-only-switch-only-sudo-pair-20260321T195146`

It was not a fair clean control, but it was still informative:

- the very first `turtleback` bring-up in `run1` failed with
  `The Wi-Fi network could not be found`
- the following raw `onionchan` connect in the same run also failed with
  `The Wi-Fi network could not be found`

That again shows the damaged state carries forward beyond the original reproducer until
the running supplicant is reset.

### Deleting `onionchan` before scanner mode did not eliminate the breakage.

To test whether NetworkManager auto-activating a saved `onionchan` profile on scanner
exit was required for the poisoned state, a stricter raw root-level variant was run on
March 21, 2026:

- connect `turtleback`
- delete any saved `onionchan`
- connect `onionchan`
- reconnect `turtleback`
- delete the newly created `onionchan` connection
- enter scanner mode
- exit scanner mode
- reconnect `turtleback`

The paired bundle is:

- `/tmp/system-only-switch-scanner-forget-before-scanner-pair-20260321T201241Z`

Results:

- `run1` successfully connected `onionchan`, restored `turtleback`, deleted the saved
  `onionchan` profile, and completed scanner enter / exit
- `run1` then failed restoring `turtleback` after scanner exit with:
  - `Activation: (wifi) association took too long, failing activation`
  - state `config -> failed (reason 'ssid-not-found')`
- `run2` started from that already damaged state, with the initial `turtleback`
  baseline bring-up failing before later steps recovered

Most importantly, the live NetworkManager / supplicant journal for the `run1`
post-scanner restore window showed:

- activation starting only for `turtleback`
- no `onionchan` auto-activation on scanner exit
- the same eventual association timeout and `ssid-not-found` failure

So the saved-profile auto-activation theory is now weaker than before:

- deleting `onionchan` before scanner mode does **not** prevent the poisoned state
- `onionchan` auto-activation is **not necessary** for the failure
- the lower-level trigger is closer to:
  - a successful real `onionchan` association
  - followed by the scanner helper round-trip
  - followed by a later managed-mode restore attempt

### The pre-scanner reconnect back to `turtleback` is not necessary either.

To test whether the trigger required the intermediate `onionchan -> turtleback`
switch before scanner mode, a fresh single-run variant was executed on March 21, 2026:

- connect `turtleback`
- connect `onionchan`
- delete the saved `onionchan` profile
- enter scanner mode
- exit scanner mode
- reconnect `turtleback`

The bundle is:

- `/tmp/system-only-onionchan-direct-scanner-20260321T201809Z`

Result:

- the single run failed restoring `turtleback` after scanner exit with:
  - `Activation: (wifi) association took too long, failing activation`
  - state `config -> failed (reason 'ssid-not-found')`

So the intermediate reconnect back to `turtleback` is **not** required. A successful
`onionchan` association followed by the scanner helper round-trip is already enough.

### Pure unmanage/remanage without the helper does not produce the same failure mode.

To separate NetworkManager-managed-state churn from the helper / stack-switch path, a
single-run variant executed:

- connect `turtleback`
- connect `onionchan`
- delete the saved `onionchan` profile
- set `wlan0` unmanaged
- set `wlan0` managed again
- reconnect `turtleback`

The bundle is:

- `/tmp/system-only-onionchan-unmanage-only-20260321T202015Z`

Result:

- this did fail, but differently:
  - `wlan0` became unavailable
  - `rescan` reported `Scanning not allowed while unavailable`
  - the `turtleback` restore failed because the device was unavailable, not because an
    association timed out

That means plain unmanage/remanage churn does **not** reproduce the same
association-timeout / `ssid-not-found` signature as the scanner path.

### The helper stack swap reproduces without `nexutil`.

To test whether monitor / promisc configuration was required, a fresh single-run
variant kept the helper round-trip but removed the `nexutil` calls:

- connect `turtleback`
- connect `onionchan`
- delete the saved `onionchan` profile
- set `wlan0` unmanaged
- run `dirtsim-nexmon-mode enable`
- return to stock mode with `dirtsim-nexmon-mode disable`
- set `wlan0` managed again
- reconnect `turtleback`

The first bundle is:

- `/tmp/system-only-onionchan-helper-only-20260321T202137Z`

That run still failed restoring `turtleback` with the same signature:

- `Activation: (wifi) association took too long, failing activation`
- state `config -> failed (reason 'ssid-not-found')`

### The extra `ip link` choreography is not necessary either.

To separate the helper stack swap from the explicit `ip link` toggles, a final fresh
single-run variant removed the manual `ip link set wlan0 up/down` steps but kept the
same helper enable / disable path:

- connect `turtleback`
- connect `onionchan`
- delete the saved `onionchan` profile
- set `wlan0` unmanaged
- run `dirtsim-nexmon-mode enable`
- run `dirtsim-nexmon-mode disable`
- set `wlan0` managed again
- reconnect `turtleback`

The bundle is:

- `/tmp/system-only-onionchan-helper-no-link-20260321T202347Z`

That run also failed restoring `turtleback` with the same signature:

- `Activation: (wifi) association took too long, failing activation`
- state `config -> failed (reason 'ssid-not-found')`

This is the strongest current narrowing below the app:

- `nexutil -m2/-p1/-m0/-p0` is **not necessary**
- the extra manual `ip link` toggles are **not necessary**
- the key remaining lower-level trigger is:
  - a successful `onionchan` association
  - followed by `dirtsim-nexmon-mode enable`
  - followed by `dirtsim-nexmon-mode disable`
  - followed by a managed-mode reconnect to `turtleback`

### The installed helper matches the local helper, and the traced command paths are identical.

On March 21, 2026, the installed `/usr/bin/dirtsim-nexmon-mode` on `dirtsim3` was
compared directly to the local source in:

- `/home/data/workspace/dirtsim/yocto/meta-dirtsim/recipes-kernel/nexmon/dirtsim-nexmon-mode/dirtsim-nexmon-mode`

They matched exactly.

Fresh `sh -x` traces were then captured for both `enable` and `disable` in:

- `/tmp/helper-trace-pair-20260321T202952Z`

The important result was:

- the traced shell command sequence for `enable` and `disable` was the same in the
  control and trigger cases
- the stripped-down `enable` / `disable` path was **not** a good pass/fail comparison,
  because it could fail even without any prior `onionchan` association

So the helper source and the shell-level command ordering are not where the control and
trigger cases diverge.

### Immediate post-`exit` failures were confounded by autoconnect policy.

Fresh `sh -x` traces of the full helper `enter` / `exit` path were captured in:

- `/tmp/helper-enter-exit-trace-pair-20260321T203354Z`

Initially, both the clean control and the `onionchan` trigger case appeared to fail if
`nmcli connection up turtleback` was called immediately after `exit`. However, that was
misleading because the helper exit path had already handed control back to
NetworkManager, and background autoconnect behavior differed between the cases.

To separate helper recovery from autoconnect policy, a post-`exit` recovery poll was
captured in:

- `/tmp/helper-exit-recovery-pair-20260321T203722Z`

That showed:

- clean control case:
  - `wlan0` remained disconnected during the 20-second poll
  - with the default saved-profile set, NetworkManager tried unrelated
    `Houseofboys` autoconnect paths during recovery
- trigger case after successful `onionchan` association:
  - `wlan0` auto-restored to `turtleback` on its own about 2 seconds after `exit`
  - it remained connected to `turtleback` through the full 20-second poll
  - the later explicit `nmcli connection up turtleback` failure was therefore an
    artifact of forcing a new activation while `turtleback` was already connected

So the `onionchan` trigger case does **not** simply mean “helper exit left Wi-Fi
broken.” In that case, helper exit plus NetworkManager policy was actually able to
restore `turtleback` automatically.

### `Houseofboys` autoconnect was masking the clean control path.

To isolate the clean control case, both saved `Houseofboys` profiles were temporarily
set to `autoconnect=no` and then restored afterward.

Bundles:

- `/tmp/helper-exit-control-no-houseofboys-20260321T204122Z`
- `/tmp/helper-exit-control-no-houseofboys-delayed-restore-20260321T204252Z`

Findings:

- with `Houseofboys` autoconnect disabled, the clean control case no longer chased that
  unrelated profile after `exit`
- the control case still did **not** auto-restore `turtleback` on its own during the
  20-second poll
- but after that settle window, an explicit:
  - `nmcli -w 75 connection up turtleback ifname wlan0`
  succeeded and completed WPA association

This is a more accurate current interpretation:

- immediate post-`exit` failures in the clean control case were largely an
  autoconnect-policy and timing confound
- helper `exit` does not necessarily leave the radio unrecoverable
- on a clean baseline, `turtleback` can be restored successfully after the helper
  round-trip if unrelated autoconnect behavior is suppressed and the restore is not
  forced immediately
- after a prior `onionchan` association, NetworkManager may auto-restore `turtleback`
  on its own without any explicit reconnect request

The remaining diagnostic question is therefore narrower:

- why does prior `onionchan` association change NetworkManager / supplicant recovery
  behavior after helper exit, especially the auto-restore path and timing?

## Next Recommended Diagnostics

1. Correlate the connect request IDs with `wpa_supplicant`, NetworkManager, and kernel
   Wi-Fi logs on `dirtsim3.local`.
2. Capture whether the Pi stops seeing `onionchan` before or during the failed attempt.
3. Compare a passing post-reboot run and a failing later run from the same Pi without
   changing app code in between.
4. Compare the first passing switch-then-scanner run against the second failing
   switch-then-scanner run to identify what state survives the first scanner round-trip.
5. Compare recovery boundaries:
   - reboot clears the state
   - `NetworkManager` restart alone does not
   - `wpa_supplicant` restart alone does
   - `nmcli radio wifi off/on` does not
   - `SIGHUP` to `wpa_supplicant` also clears it, but only because the PID changes
6. Use the debug pair as the current highest-signal evidence for what the stale
   supplicant is carrying:
   - BSSID ignore-list state
   - repeated scan-trigger failures
   - different BSSID selection from the fresh process
7. Focus next on the interaction between a completed `onionchan` association and the
   later scanner-mode round-trip, because:
   - cancel alone passes
   - scanner alone passes
   - switch alone passes
   - cancel plus scanner passes
   - switch plus scanner fails on the second run
   - switch-forget-scanner still fails on the second run
8. Use stable `eth1` control-plane access (`192.168.1.142`) for future diagnostics
   instead of relying on `dirtsim3.local`.
9. Treat Wi-Fi power-save A/B as a lower-priority branch unless a system-only repro
   appears again.
10. Track whether post-test failures are actual Wi-Fi loss or stale host discovery
   (`dirtsim3.local` resolving to an old address).
11. Keep product fixes limited to clearly reproducible logic bugs, not recovery code for
   a broken Wi-Fi subsystem state.
12. Be careful with timing-only restore tweaks. One attempt to "win the race" by
    restoring immediately made the box wedge harder instead of better.

## Status

Steps 1 through 3 above have now been completed once for the paired run on
March 21, 2026. The heavier system-only churn test also completed once successfully
without reproducing the failure. A direct `dirtsim-cli network` paired run also
completed successfully without reproducing the failure. A stable-control-plane
backend-only paired run then reproduced the failure again. A later isolated
backend-only cancel/reconnect pair also passed twice, which means cancel alone is not
enough. A second confirmation round repeated that same contrast: full backend-only
pass/fail, cancel-only pass/pass. A new scanner-only backend pair then also passed
pass/pass, which means scanner mode alone is not enough either. A cancel-plus-scanner
pair then also passed pass/pass, which means cancel is not the missing ingredient.
A switch-plus-scanner pair then reproduced the pass/fail pattern without any cancel,
while a switch-only backend pair still passed pass/pass. The remaining work is now to
focus on the interaction between a completed real `onionchan` switch and the later
scanner-mode round-trip. A follow-up switch-forget-scanner pair also failed, so saved
`onionchan` auto-activation is not the whole story. Recovery testing then showed that
`NetworkManager` restart alone does not clear the bad state, while `wpa_supplicant`
restart alone does. A later Wi-Fi radio off/on test did **not** recover the failure
and kept the same `wpa_supplicant` PID alive, which pushes the likely bad state even
more strongly toward the running supplicant process or state tightly coupled to it,
rather than plain NetworkManager policy or a simple radio/device reset. A later
`SIGHUP` test also recovered, but only by changing the PID, so it does not establish a
smaller in-process reset boundary. Finally, a temporary `-dd -t` debug pair showed the
stale process building BSSID ignore-list state and scan-trigger failures before NM
surfaces `no-secrets`, while the fresh post-restart process begins with clear BSSID
state and connects successfully. Finally, a raw root-level switch-plus-scanner pair
reproduced the follow-on `onionchan` failure without any dirtsim code in the control
path, while clean raw switch-only and clean raw scanner-only controls still passed.
That means the current problem is now reproducible below the app at the
NetworkManager / `wpa_supplicant` / scanner-helper boundary. A later stricter raw test
then deleted `onionchan` before scanner mode and still reproduced a post-scanner
restore failure, which means saved-profile auto-activation is not necessary for the
bad state either. Later single-run splits then showed that the intermediate
`onionchan -> turtleback` reconnect is not required, `nexutil` is not required, and
the extra `ip link` choreography is not required either. The strongest remaining
boundary is now the helper-driven stack swap itself after a successful `onionchan`
association. Product behavior should still not be changed to paper over that
transient subsystem state.

## Direct NM Autoconnect Comparison

At `2026-03-21 20:51 UTC`, a direct NetworkManager-only comparison clarified an
important policy difference that had been hiding inside the helper experiments.
The bundle is in `/tmp/nm-autoconnect-pair-20260321T205136Z`.

- `disconnect_only`:
  - baseline `turtleback`
  - `nmcli device disconnect wlan0`
  - 20 second poll
- `onionchan_delete`:
  - baseline `turtleback`
  - `nmcli device wifi connect onionchan ...`
  - `nmcli connection delete onionchan`
  - 20 second poll

The `disconnect_only` case stayed disconnected for the full poll window with:

```text
GENERAL.STATE:30 (disconnected)
GENERAL.REASON:39 (Device disconnected by user or client)
```

The `onionchan_delete` case behaved differently. It first landed in:

```text
GENERAL.STATE:30 (disconnected)
GENERAL.REASON:38 (The device's active connection disappeared)
```

Then about 3 seconds later, NetworkManager auto-activated `turtleback` by itself:

```text
policy: auto-activating connection 'turtleback'
device (wlan0): Activation: starting connection 'turtleback'
```

This means the control-vs-trigger difference is not purely "helper exit broke the
stack" versus "helper exit did not break the stack." There is also a plain NM policy
difference between:

- an explicit user/client disconnect (`reason=39`), which suppresses autoconnect, and
- a disappeared active connection (`reason=38`), which still allows autoconnect.

## Pre-Helper State Comparison

At `2026-03-21 20:54 UTC`, the next comparison tested the minimal helper boundary from
the `reason=38` pre-state. The bundle is in
`/tmp/helper-prestate-pair-20260321T205418Z`.

- `reason38_helper`:
  - baseline `turtleback`
  - connect `onionchan`
  - delete `onionchan`
  - `managed no -> helper enable -> helper disable -> managed yes`
  - 20 second poll
- `reason39_helper`:
  - same setup, then try `nmcli device disconnect wlan0` before the helper boundary

What actually happened:

- after `onionchan` delete, both cases were already in:

```text
GENERAL.STATE:30 (disconnected)
GENERAL.REASON:38 (The device's active connection disappeared)
```

- the extra `nmcli device disconnect wlan0` did not change that state because the
  device was already inactive:

```text
Error: Device 'wlan0' ... disconnecting failed: This device is not active
```

- after the helper boundary, both cases auto-restored `turtleback` within about 2 to 5
  seconds

So this pair did **not** yet force a real `reason=39` into the helper path. What it
did show is that the minimal helper boundary is compatible with clean auto-restore
when the pre-helper state is already the `reason=38` "active connection disappeared"
state.

## Current Interpretation

The newer evidence points to a mixed explanation:

- the helper-driven stack swap is still part of the reproducer boundary for the bad
  follow-on state
- but some of the earlier immediate post-exit "failure" signal was actually
  NetworkManager autoconnect policy and timing, not hard helper breakage
- `reason=38` versus `reason=39` is now a concrete part of that policy story

The next useful refinement is to force a **real** `reason=39` before the helper
boundary in a way that leaves `wlan0` active first, then see whether post-exit
auto-restore disappears. That will tell us how much of the control-vs-trigger
difference is explained by autoconnect suppression versus a deeper state change.

## Connected `turtleback` Before Helper: Minimal Versus Full

At `2026-03-21 20:57 UTC`, the next split used the same pre-helper state in both
cases:

- connect `onionchan`
- delete `onionchan`
- wait for `turtleback` to auto-restore
- confirm:

```text
GENERAL.STATE:100 (connected)
GENERAL.CONNECTION:turtleback
GENERAL.REASON:0 (No reason given)
```

Then it compared:

- `connected_minimal`:
  - `managed no -> helper enable -> helper disable -> managed yes`
- `connected_full`:
  - full `/usr/bin/dirtsim-nexmon-mode enter`
  - full `/usr/bin/dirtsim-nexmon-mode exit`

The bundle is in `/tmp/helper-connected-after-onion-20260321T205719Z`.

Results:

- `connected_minimal` auto-restored `turtleback` within about 2 to 5 seconds after the
  stock stack came back.
- `connected_full` stayed disconnected for the full 20 second poll after `exit`.

The NetworkManager journal explains the difference. In `connected_full`, helper
`enter` explicitly did:

```text
device (wlan0): state change: activated -> deactivating (reason 'user-requested')
audit: op="device-disconnect" interface="wlan0" ... result="success"
device (wlan0): state change: deactivating -> disconnected (reason 'user-requested')
```

After the later `exit`, the device returned to:

```text
device (wlan0): state change: unavailable -> disconnected (reason 'supplicant-available')
```

but there was no `policy: auto-activating connection 'turtleback'` event during the
entire poll window.

This is the strongest evidence so far that the helper's own disconnect step is a key
policy difference:

- the same stack swap without the disconnect step auto-restores
- the full helper path from an active `turtleback` connection does not

That does not yet prove the disconnect step is the whole stale-supplicant trigger, but
it does explain a large part of the earlier control-versus-trigger recovery mismatch.

## Repeated Minimal Pair Versus Repeated Full Pair

At `2026-03-21 21:01 UTC`, a repeated-pair comparison tested whether the helper's
disconnect step was actually required for the second-run `onionchan` failure. The
bundle is in `/tmp/helper-full-vs-minimal-pairs-20260321T210049Z`.

Each pair did two runs without restarting `wpa_supplicant` between run 1 and run 2.
Each run:

1. connected `onionchan`
2. deleted `onionchan`
3. returned to `turtleback`
4. executed either the minimal stack-swap path or full helper `enter/exit`
5. restored `turtleback`

Results:

- `minimal` pair:
  - run 1: `onionchan` connect succeeded
  - run 2: `onionchan` connect failed immediately with:

```text
Error: Connection activation failed: Secrets were required, but not provided.
```

- `full` pair:
  - run 1: `onionchan` connect succeeded
  - run 2: `onionchan` connect failed with the **same** immediate error

This is the important correction:

- the helper disconnect step still explains the post-helper recovery difference
- but it is **not required** for the second-run `onionchan` failure

In other words, the stale state is already present by the time run 2 begins its next
`onionchan` association attempt. The full helper path changes how recovery back to
`turtleback` behaves afterward, but the core follow-on failure reproduces even with the
minimal stack-swap boundary.

That pushes suspicion back down toward the lower-level Wi-Fi stack state that survives:

- a successful `onionchan` association
- `onionchan` connection removal
- subsequent recovery to `turtleback`
- later re-attempt to associate to `onionchan`

The helper disconnect step is now best understood as an additional recovery-policy
difference, not the root trigger by itself.

## Repeated `onionchan` Delete Pair Without Any Helper

At `2026-03-21 21:08 UTC`, a direct control pair removed scanner/helper work
completely. The bundle is in `/tmp/repeated-onion-delete-pair-20260321T210822Z`.

Each run:

1. connect `onionchan`
2. delete `onionchan`
3. let NetworkManager auto-restore `turtleback`

Then the same sequence was repeated a second time without restarting
`wpa_supplicant`.

Both runs passed:

- run 1 reached `onionchan`, deleted it, and auto-restored `turtleback`
- run 2 did the same

This is a strong negative control:

- successful `onionchan` association plus connection deletion plus auto-restore is
  **not enough** by itself to trigger the next-run failure
- scanner/helper behavior is still required somewhere in the failing path

## Repeated Explicit Reconnect Pair With Minimal Stack Swap

At `2026-03-21 21:09 UTC`, the next pair removed the delete/auto-restore return path
but kept the minimal stack swap. The bundle is in
`/tmp/repeated-onion-explicit-minimal-pair-20260321T210937Z`.

Each run:

1. connect `onionchan`
2. explicitly activate `turtleback`
3. run the minimal stack swap:
   - `managed no`
   - helper `enable`
   - helper `disable`
   - `managed yes`
4. restore `turtleback` if needed

Results:

- run 1 passed
- run 2 failed at the next `onionchan` connect

The journal in this case is especially useful because it removes the
delete/auto-restore ambiguity. On run 2, the sequence was:

```text
Trying to associate with 24:f5:a2:07:48:2e (SSID='onionchan' freq=5200 MHz)
device (wlan0): Activation: (wifi) association took too long
device (wlan0): Activation: failed for connection 'onionchan'
```

So the delete/auto-restore path is **not** required. A successful `onionchan`
association followed by an explicit reconnect to `turtleback`, then the actual stock
`<->` nexmon stack swap, is already enough to make the next `onionchan` association
fail.

## Repeated Explicit Reconnect Pair With Manage Toggle Only

At `2026-03-21 21:11 UTC`, the next control kept the same explicit
`onionchan -> turtleback` return path but removed the actual stack swap. The bundle is
in `/tmp/repeated-onion-explicit-manage-pair-20260321T211141Z`.

Each run:

1. connect `onionchan`
2. explicitly activate `turtleback`
3. `nmcli device set wlan0 managed no`
4. `nmcli device set wlan0 managed yes`
5. restore `turtleback` if needed

Both runs passed:

- run 1 reached `onionchan` and came back to `turtleback`
- run 2 also reached `onionchan` and came back to `turtleback`

This is the strongest boundary so far:

- `managed no/yes` ownership churn is **not** enough
- pure `onionchan` connect/delete or connect/reconnect churn is **not** enough
- the actual stock/nexmon stack swap after a successful `onionchan` association is the
  necessary added step in the smallest currently known reproducer

## Current Smallest Reproducer

As of the end of March 21, 2026, the tightest safe reproducer is:

1. start from a healthy `turtleback` connection
2. connect `onionchan` successfully
3. explicitly reconnect `turtleback`
4. perform the minimal stack swap:
   - `nmcli device set wlan0 managed no`
   - `/usr/bin/dirtsim-nexmon-mode enable`
   - `/usr/bin/dirtsim-nexmon-mode disable`
   - `nmcli device set wlan0 managed yes`
5. reconnect `turtleback` if needed
6. attempt `onionchan` again

That second `onionchan` attempt can then fail with the characteristic association-timeout
signature.

## File-Based `wpa_supplicant` Debug On The Smallest Reproducer

At `2026-03-21 22:09 UTC`, the smallest reproducer was rerun with
`wpa_supplicant -u -dd -t -f /tmp/dirtsim-wpa-debug.log` and targeted snapshots around
the stack swap. The bundle is in `/tmp/minimal-stackswap-debug-20260321T220934Z`.

This run added three useful facts.

### 1. The stack swap tears down the supplicant interface and flushes all BSS state.

During the first `managed no -> enable -> disable -> managed yes` cycle, the debug log
shows `wpa_supplicant` removing `wlan0` and the sibling `p2p-dev-wlan0`, then flushing
its known BSS entries:

```text
p2p-dev-wlan0: BSS: Remove id 0 BSSID 24:f5:a2:07:48:2f SSID 'onionchan' due to wpa_bss_flush
p2p-dev-wlan0: BSS: Remove id 1 BSSID 24:f5:a2:07:48:2e SSID 'onionchan' due to wpa_bss_flush
p2p-dev-wlan0: BSS: Remove id 2 BSSID 44:10:91:6d:8e:30 SSID 'turtleback' due to wpa_bss_flush
```

So the stack swap is not just toggling monitor support. It is causing a real
interface-removal / interface-recreation event from the supplicant's point of view.

### 2. The restored stock interface comes back as a new interface instance.

The snapshots show:

- before helper:
  - `wlan0` ifindex `4`
  - connected on `turtleback`
  - hardware address `88:A2:9E:14:62:62`
- after `enable`:
  - Nexmon module version visible: `6.12.2-nexmon`
  - `wlan0` is `NO-CARRIER`
  - randomized current MAC `9E:B6:45:E3:8B:BA`
  - permanent MAC still reported separately
- after `disable`:
  - stock `wlan0` is still `NO-CARRIER`
  - current MAC is randomized again (`32:8A:ED:8E:F6:81`)
  - interface ifindex is now `6`, not `4`

By the time `turtleback` is restored, the interface is healthy again and the current MAC
is back to `88:A2:9E:14:62:62`, but it is still the post-swap interface instance
(`ifindex 6`).

### 3. The failing second `onionchan` attempt is a concrete `2f -> ignore-list -> 2e`
### sequence.

In this debug run, after the swap and delayed recovery, the second `onionchan` connect
attempt did this:

```text
wlan0: Trying to associate with 24:f5:a2:07:48:2f
wlan0: Authentication with 24:f5:a2:07:48:2f timed out.
Added BSSID 24:f5:a2:07:48:2f into ignore list, ignoring for 10 seconds
wlan0: CTRL-EVENT-SCAN-FAILED ret=-1 retry=1
WPS: AP[0] 24:f5:a2:07:48:2f ... bssid_ignore=2
WPS: AP[1] 24:f5:a2:07:48:2e ... bssid_ignore=0
wlan0: Trying to associate with 24:f5:a2:07:48:2e
```

That is the clearest root-level signature so far.

## Updated Interpretation

The current best read is:

- the necessary step is still the real stock/nexmon stack swap
- that swap causes `wpa_supplicant` to lose its interface instance and flush BSS state
- the restored interface comes back as a new instance with a new ifindex
- after that reset, the next `onionchan` attempt consistently falls into the
  `2f auth timeout -> ignore list -> scan failure -> 2e fallback` path

This still points below the app and below plain NetworkManager policy. The live
question is now what about the stack swap and re-enumerated `wlan0` leaves the
supplicant/driver state in a way that makes the next `2f` authentication stall.

## Post-Swap Pinned-BSSID Follow-Up

At `2026-03-21 22:26 UTC`, the next test forced the post-swap `onionchan` attempt to a
specific BSSID instead of letting the supplicant choose between `2f` and `2e`. The
bundle is in `/tmp/post-swap-pinned-bssid-20260321T222609Z`.

Each case used the same minimized reproducer:

1. connect `onionchan`
2. reconnect `turtleback`
3. perform the minimal stack swap
4. restore `turtleback`
5. activate a BSSID-pinned `pin-test` profile for `onionchan`

The two cases were:

- `pin_2f`: `24:F5:A2:07:48:2F`
- `pin_2e`: `24:F5:A2:07:48:2E`

Both failed in the same way:

- the pinned connection started normally
- `wpa_supplicant` kept retrying the pinned BSSID only
- NetworkManager logged repeated `association took too long`
- the connect never reached a completed WPA association before the `75s` CLI timeout

The journal shows the intended BSSID pinning worked:

- `pin_2f`:

```text
wlan0: Trying to associate with 24:f5:a2:07:48:2f (SSID='onionchan' freq=5660 MHz)
device (wlan0): Activation: (wifi) association took too long
```

- `pin_2e`:

```text
wlan0: Trying to associate with 24:f5:a2:07:48:2e (SSID='onionchan' freq=5200 MHz)
device (wlan0): Activation: (wifi) association took too long
```

So the post-swap failure is no longer explainable as:

- "the supplicant just picked the wrong `onionchan` radio"
- "only `2f` is bad after the swap"
- or "only `2e` is bad after the swap"

After the stack swap, both `onionchan` radios can be driven into the same
association-timeout state on demand.

## Current Best Boundary

The best current boundary is now:

- plain NetworkManager churn: not enough
- `managed no/yes` churn: not enough
- repeated `onionchan` connect/delete or connect/reconnect without the stack swap: not
  enough
- real stock/nexmon stack swap after a successful `onionchan` association: enough
- after that swap, both pinned `onionchan` BSSIDs can time out in association

That pushes the investigation even more strongly toward driver / firmware /
supplicant state that survives the stack swap, rather than AP selection policy.

## Stock-Only Reload Pinned-BSSID Control

At `2026-03-21 22:42 UTC`, a cleaner control tested whether generic stock
`brcmfmac` reload churn is sufficient without ever loading Nexmon artifacts. The
bundle is in `/tmp/post-stock-reload-pinned-bssid-20260321T224228Z`.

Each case used this sequence:

1. restart `wpa_supplicant` to establish a fresh baseline
2. connect `onionchan`
3. reconnect `turtleback`
4. perform a double stock reload only:
   - disconnect `wlan0`
   - `managed no`
   - `modprobe -r brcmfmac_wcc`
   - `modprobe -r brcmfmac`
   - `modprobe brcmfmac`
   - repeat the unload/reload once more
   - `managed yes`
5. wait until both `turtleback` and `onionchan` are visible in scans again
6. reconnect `turtleback`
7. attempt a BSSID-pinned `onionchan` connect

Two pinned cases were run:

- `pin_2f`: `24:F5:A2:07:48:2F`
- `pin_2e`: `24:F5:A2:07:48:2E`

Both **succeeded**:

- after the stock-only reload, scan visibility returned for both `onionchan`
  radios and `turtleback`
- `turtleback` restored cleanly
- the pinned `pin-test` connection then activated successfully for both `2f`
  and `2e`
- the device finished back on `turtleback`

So the failure does **not** follow from generic `brcmfmac` unload/reload churn
by itself, even when the control is made close to the helper path and both
radios are tested explicitly after the reload.

## Updated Interpretation

This strengthens the current boundary again:

- plain NetworkManager churn: not enough
- `managed no/yes` churn: not enough
- stock `brcmfmac` unload/reload churn: not enough
- real stock/nexmon stack swap after a successful `onionchan` association:
  enough
- after the real stock/nexmon swap, both pinned `onionchan` radios can still be
  driven into the association-timeout state

So the evidence now points more specifically at something introduced by the
actual Nexmon module / firmware transition, or by how the stock stack comes back
after that transition, rather than at generic supplicant or NetworkManager
recovery from a stock driver reload.

## Real Stack Swap With Immediate `wpa_supplicant` Restart

At `2026-03-21 22:45 UTC`, the next discriminator tested the real stock/nexmon
stack swap again, but restarted `wpa_supplicant` immediately after the helper
returned control to the stock stack and before attempting the next
`onionchan` connect. The bundle is in
`/tmp/post-swap-with-wpa-restart-pinned-bssid-20260321T224526Z`.

Each case used this sequence:

1. restart `wpa_supplicant` to establish a fresh baseline
2. connect `onionchan`
3. reconnect `turtleback`
4. perform the real helper round-trip:
   - `managed no`
   - `/usr/bin/dirtsim-nexmon-mode enable`
   - `/usr/bin/dirtsim-nexmon-mode disable`
   - `managed yes`
5. restart `wpa_supplicant` immediately
6. wait until both `turtleback` and `onionchan` are visible again
7. reconnect `turtleback`
8. attempt a BSSID-pinned `onionchan` connect

Two pinned cases were run:

- `pin_2f`: `24:F5:A2:07:48:2F`
- `pin_2e`: `24:F5:A2:07:48:2E`

Both **succeeded**:

- after the real helper round-trip, `wpa_supplicant` restart restored normal scan
  visibility for both `onionchan` radios and `turtleback`
- `turtleback` restored cleanly
- the pinned `pin-test` connection then activated successfully for both `2f`
  and `2e`
- the device finished back on `turtleback`

This is a strong discriminator against a lower-level "the stock stack itself is
still broken after the nexmon round-trip" explanation. The same real stock/nexmon
swap that previously caused both pinned BSSIDs to fail became healthy when the
surviving `wpa_supplicant` process was replaced before the next association.

## Refined Boundary

The current best boundary is now:

- plain NetworkManager churn: not enough
- `managed no/yes` churn: not enough
- stock `brcmfmac` unload/reload churn: not enough
- real stock/nexmon stack swap with the same `wpa_supplicant` process surviving:
  enough to reproduce the bad state
- real stock/nexmon stack swap followed by immediate `wpa_supplicant` restart:
  not enough to reproduce the bad state in the pinned-BSSID test

So the most specific current interpretation is:

- the helper-driven stock/nexmon transition is the necessary perturbation
- but the observed post-swap failure depends on stale `wpa_supplicant` process
  state surviving that transition
- replacing the supplicant process clears the bad state even after the real
  nexmon round-trip

That pulls the root boundary back up from generic "driver/firmware state after
the swap" to "state retained by the long-lived `wpa_supplicant` process across
the swap", or to a specific interaction between that process and the swapped
interface instance.

## Minimal Reproducer With Immediate `wpa_supplicant` Restart

At `2026-03-21 22:49 UTC`, the current smallest reproducer was rerun as a normal
SSID-based pair with one additional step only: restart `wpa_supplicant`
immediately after the real stock/nexmon helper round-trip. The bundle is in
`/tmp/minimal-swap-with-wpa-restart-pair-20260321T224903Z`.

Each run used:

1. connect `onionchan`
2. reconnect `turtleback`
3. perform the real helper round-trip:
   - `managed no`
   - `/usr/bin/dirtsim-nexmon-mode enable`
   - `/usr/bin/dirtsim-nexmon-mode disable`
   - `managed yes`
4. restart `wpa_supplicant`
5. wait until `turtleback` and `onionchan` are visible again
6. reconnect `turtleback`
7. attempt a normal SSID-based `onionchan` connect

Both runs **passed**:

- `run1`: the post-swap `onionchan` reconnect succeeded and the device finished
  back on `turtleback`
- `run2`: the same sequence succeeded again with no intervening reboot

This is the closest match yet to the original failing reproducer, and it no
longer shows the old `pass -> fail` pattern once the post-swap
`wpa_supplicant` restart is inserted.

## Current Best Reading

The most specific interpretation at this point is:

- the real stock/nexmon stack swap is the necessary perturbation
- the failure is not explained by generic NetworkManager churn or generic stock
  `brcmfmac` reloads
- the bad state depends on the same long-lived `wpa_supplicant` process
  surviving the real helper round-trip
- replacing that process after the round-trip clears the bad state and restores
  normal association behavior, including in the normal SSID-based reproducer

That means future product work should not start from "make the app survive a
broken Wi-Fi subsystem." The first system-level question is now whether the
correct fix is to restart `wpa_supplicant` as part of handing control back to
the stock stack, or whether a smaller targeted supplicant reset exists on this
image.

## Temporary Helper-Level Restart In The Real Backend Reproducer

At `2026-03-21 23:27 UTC`, the next experiment moved from raw shell reproducers
back to the real backend functional path, but still kept the change below the
app. A temporary helper variant was installed on `dirtsim3` that changed only
the `exit` path:

- after `disable_stack`
- after `bring_wlan_up`
- after `reclaim_network_manager`
- restart `wpa_supplicant`
- then continue with the normal rescan / restore flow

The helper backup stayed on the device at
`/usr/bin/dirtsim-nexmon-mode.codex-backup-20260321`, and each probe restored
the original helper on exit. Two bounded single-run probes were then executed
against the real backend test:

- `canSwitchThenScannerBackendOnly`
- Wi-Fi config: `/tmp/wifi-functional-test.json`
- helper patch active only for the duration of the probe
- helper automatically restored afterward

The bundles are:

- `/tmp/helper-backend-single-probe-20260321T232727Z`
- `/tmp/helper-backend-single-probe-20260321T232832Z`

Both runs **succeeded** with no reboot in between:

- first run: `{"duration_ms":38309,"name":"canSwitchThenScannerBackendOnly","result":{"success":true}}`
- second run: `{"duration_ms":46755,"name":"canSwitchThenScannerBackendOnly","result":{"success":true}}`

After each run:

- the device was back on `turtleback`
- the helper had been restored to the original script
- SSH remained healthy

This is the strongest system-side result so far. The actual backend reproducer
that previously showed `pass -> fail` can be turned into `pass -> pass` by
restarting `wpa_supplicant` as part of the helper handoff back to the stock
stack.

## Updated Practical Conclusion

At this point the evidence supports all of the following:

- the backend failure is not best understood as an app-layer bug
- the decisive boundary is the long-lived `wpa_supplicant` process surviving the
  real stock/nexmon round-trip
- restarting `wpa_supplicant` at helper exit is a credible mitigation on the
  real backend path, not just in lower-level shell experiments

That does not yet prove it is the final production fix, but it is now the best
current fix candidate and the best current explanation of the reproduced
failure.

## Stable Probe Setup

After a few reboots, `/tmp/wifi-functional-test.json` kept disappearing and
causing false negatives in bounded probe runs. For the later probes, the test
config was moved to a stable path:

- `/home/dirtsim/wifi-functional-test.json`

The config contents stayed the same:

- baseline: `turtleback`
- target/cancel SSID: `onionchan`
- password: shared lab password

## Cancel-Only Backend Control

At `2026-03-21 23:47 UTC`, a bounded single-run probe of
`canCancelWifiConnectBackendOnly` was executed with the original helper and the
stable Wi-Fi config path. The bundle was
`/tmp/backend-single-probe-20260321T234735Z`.

It passed cleanly:

- `{"duration_ms":19884,"name":"canCancelWifiConnectBackendOnly","result":{"success":true}}`

After the run:

- `wlan0` was back on `turtleback`
- the original helper was still installed

So the cancel-only backend path remains healthy by itself.

## Cancel Prelude Plus Patched Narrow Scanner Probe

Immediately after that cancel-only control, the patched helper probe for
`canSwitchThenScannerBackendOnly` was run again in the same boot. That probe
returned `exit_code=0`, which means the backend test completed successfully
under the helper variant that restarts `wpa_supplicant` on exit.

This matters because it rules out a simpler explanation:

- it is **not** the case that the earlier canceled-connect sequence poisons the
  whole system globally and causes the later scanner probe to fail regardless
  of session boundaries

The narrower "switch then scanner" backend flow can still be healthy after a
successful cancel-only backend run, as long as the helper-level restart is in
place.

## Single-Session Cancel Then Scanner With Helper Restart

The next built-in split was `canCancelThenScannerBackendOnly`, which keeps the
canceled connect and the later scanner round-trip in the **same** backend test
session, but still omits the later successful reconnect to `onionchan`. That
probe used the helper variant that restarts `wpa_supplicant` on scanner exit.

The bundle was `/tmp/helper-backend-single-probe-20260321T234930Z`.

This one failed, but the failure mode changed:

- it did **not** fail with the old stale-association signature
- instead it failed during `ScannerModeExit` restore:

```json
{"duration_ms":45184,"failure_screenshot_path":"/tmp/dirtsim-functional-test-canCancelThenScannerBackendOnly-1774137022277.png","name":"canCancelThenScannerBackendOnly","result":{"error":"OS ScannerModeExit failed: Failed to restore Wi-Fi connection to 'turtleback': Connection 'turtleback' is not available on device wlan0 because device is not available; cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: Connection 'turtleback' is not available on device wlan0 because device is not available","success":false}}
```

The post-failure snapshot at that point showed:

- `wlan0` disconnected
- `GENERAL.REASON:42 (The supplicant is now available)`

That is a different, narrower problem: after the helper-level restart, scanner
exit can return before the stock Wi-Fi device is ready enough for the immediate
restore attempt.

## What Changed When Extra Readiness Was Added

Two more temporary helper variants were tried after that:

1. restart `wpa_supplicant` on exit, then wait for `wlan0` to leave the
   unavailable state using `nmcli`
2. restart `wpa_supplicant` on exit, then use a fixed `sleep 5` settle delay

In both cases, the behavior changed again:

- the earlier fast `device is not available` failure no longer happened at the
  same point
- instead, `canCancelThenScannerBackendOnly` stopped returning within the
  bounded probe window
- the Pi stayed pingable on `192.168.1.142`
- but new SSH sessions stopped producing a banner, which is the same degraded
  control-plane pattern seen in earlier "wedged" runs

So extra helper-side waiting is not obviously sufficient, and at least these two
simple variants did not produce a clean pass.

## Narrowed State Of The Problem

With the latest probes, the remaining boundary is now more specific:

- original helper:
  - `canSwitchThenScannerBackendOnly` reproduces the old `pass -> fail`
    association problem
- helper restart on exit:
  - `canSwitchThenScannerBackendOnly` becomes healthy (`pass -> pass`)
  - `canCancelWifiConnectBackendOnly` remains healthy
  - `canCancelThenScannerBackendOnly` no longer fails on stale association
    state, but now exposes a device-readiness / control-plane problem after
    scanner exit

So the remaining unresolved issue is no longer "does restarting
`wpa_supplicant` help?" It does, for the original reproducer. The unresolved
issue is how to reintroduce that restart without destabilizing the
cancel-plus-scanner path that keeps both phases in a single backend session.

## Instrumented Scanner Exit Shows A Real Device-Availability Gap

On March 22, 2026, os-manager was temporarily instrumented around
`ScannerModeExit` and `restoreWifiAfterScannerMode()` to log the cached
network snapshot:

- immediately after helper `exit`
- immediately after the post-exit scan request
- before and after each restore attempt

With that instrumentation deployed, the helper-restart variant was used again
for a bounded single-run probe of `canCancelThenScannerBackendOnly`. The bundle
was:

- `/tmp/helper-backend-single-probe-20260322T001205Z`

It failed with the same fast error:

```json
{"duration_ms":45229,"failure_screenshot_path":"/tmp/dirtsim-functional-test-canCancelThenScannerBackendOnly-1774138377470.png","name":"canCancelThenScannerBackendOnly","result":{"error":"OS ScannerModeExit failed: Failed to restore Wi-Fi connection to 'turtleback': Connection 'turtleback' is not available on device wlan0 because device is not available; cleanup failed: Failed to restore WiFi network 'turtleback': WiFi connect failed for turtleback: Connection 'turtleback' is not available on device wlan0 because device is not available","success":false}}
```

The important timing came from the live journal tail during the run:

- `00:12:46`: the helper restarted `wpa_supplicant`
- `00:12:48.905`: helper `exit` returned to os-manager
- `00:12:48.905`: os-manager logged `After scanner helper exit` with:
  - `status.connected=false`
  - `access_points('turtleback')=none`
  - no active BSSID
- `00:12:50.905`: restore attempt `1/3` began
- `00:12:50.917`: NetworkManager rejected activation with:
  - `Connection 'turtleback' is not available on device wlan0 because device is not available`
- attempts `2/3` and `3/3` failed the same way
- only after the test had already failed did NetworkManager recover the device:
  - `re-acquiring supplicant interface (#1)`
  - `unavailable -> disconnected`
  - then a later external `nmcli` reconnect to `turtleback` succeeded

The saved bundle snapshot is consistent with that live timing:

- `device_status.txt`: `wlan0:wifi:disconnected`
- `wlan0.txt`: `GENERAL.REASON:42 (The supplicant is now available)`

So this is now much more specific than before:

- the helper-level `wpa_supplicant` restart does clear the old stale
  association state
- but in the single-session cancel-plus-scanner case, os-manager starts Wi-Fi
  restore while the restarted supplicant/device stack is still not available to
  NetworkManager

That means the remaining question is not "should we retry association longer?"
The immediate failure happens earlier than association. The real remaining
question is what readiness condition should gate restore after the helper-level
supplicant restart.

## Waiting For NetworkManager Device Readiness Fixes The Narrow Cancel+Scanner Reproducer

After the instrumented failure above, a new temporary helper variant was tried.
It still restarted `wpa_supplicant` on scanner exit, but instead of returning
immediately, it waited for `nmcli -t -f GENERAL.STATE device show wlan0` to
leave the `20 (unavailable)` state and reach a normal NetworkManager device
state such as:

- `30 (disconnected)`
- `50 (connecting)`
- `100 (connected)`

Only after that did it request a Wi-Fi rescan and return to os-manager.

Using that helper variant:

- `canCancelThenScannerBackendOnly` passed once in
  `/tmp/helper-backend-single-probe-20260322T001603Z`
- it then passed again immediately in the same boot in
  `/tmp/helper-backend-single-probe-20260322T001725Z`

The first passing bundle reported:

```json
{"duration_ms":51037,"name":"canCancelThenScannerBackendOnly","result":{"success":true}}
```

During the live journal tail for the first pass, the important difference was:

- after scanner exit, `wlan0` first reached plain `disconnected`
- os-manager then launched the `turtleback` restore connect
- that restore connect completed successfully on attempt `1/3`
- os-manager logged `Scanner mode exited. Restored Wi-Fi SSID 'turtleback'.`

This is the strongest current result:

- helper-level `wpa_supplicant` restart is still needed to clear the old stale
  post-swap state
- but the single-session cancel-plus-scanner path also needs a readiness gate
  that waits for NetworkManager to see `wlan0` as available again before
  restore starts

So the narrow reproducer is no longer mysterious. The missing piece was not an
arbitrary sleep; it was a specific handoff condition at the NetworkManager
device boundary.

## The Broader Backend Test Still Looks Less Stable

After the two narrow passes above, the broader backend test
`canExerciseWifiAndScannerBackendOnly` was started under the same temporary
helper variant.

Early in that run, the live state looked promising:

- `wlan0` reached `connecting (configuring): turtleback`

But the broader probe then drifted back into the older degraded control-plane
pattern:

- the Pi stayed pingable on `192.168.1.142`
- fresh SSH sessions stopped producing a banner
- the bounded wrapper had still not returned during observation

So the current state is split:

- narrow cancel-plus-scanner reproducer:
  - fixed by helper restart + NetworkManager device-readiness wait
- broader backend path:
  - still appears capable of destabilizing the control plane under the same
    temporary helper variant

That means the new readiness gate is a real and important part of the answer,
but it may not be the whole answer for the broadest backend workflow.
