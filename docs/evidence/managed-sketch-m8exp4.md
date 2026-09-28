# Nano managed-sketch M8 first passing console — 2026-09-27

The user reported that the preceding `m8exp3` Blink B image blinked D13 for
hours, but D13 was solid on after the board sat overnight while the macOS host
also slept. The user then returned the Nano to `RPI-RP2` ROM mode. This
observation does not identify whether sleep, USB reconnection, or a firmware
fault caused the stopped blink. No diagnostics were captured at the moment of
failure; entering ROM mode ended that boot.

The host was macOS 27.0 (build 26A428). The board is the previously selected
Arduino Nano RP2040 Connect, ROM flash ID `5031503337360009`. Board revision
and cable/hub details were not recorded. Pinned `picotool` read that ID before
installation. `flash_initial.py` regenerated the `m8exp4` Blink B composite,
checked its source and artifact hashes, loaded it, verified every UF2 block,
and rebooted. The exact full-composite UF2 SHA-256 is
`545967f1606f0c366c6bba57640ccea68e620c6ad23463534da328cd82722bea`.
The resident UF2 SHA-256 is
`d389931e56b4c0c6d2027d26d4fd2104abbed6f1d358767560486151d2fd8d63`;
the module capsule SHA-256 is
`62b6e95863d6c714c935484014084a6d522b7e8654ca80155713d60e60855f4c`.

FMGO hello at `192.168.77.1:7420` reported firmware `0.1.0-m8exp4`, board
`nano_rp2040_connect`, device ID `5031503337360009`, and boot ID
`c22e2e7527750092`. A first 256-byte fragmented, exact echo passed in
0.237 s. Before stream open, epoch/ack were both zero and loop boundaries
were 27,146. After open, epoch/ack were both one, directions were enabled,
and boundaries increased to 27,159. After echo, boundaries reached 27,398;
backend RX/TX each reported 256 bytes. Application RX peak was 256 and TX
peak was 64, with zero discards. Sketch output peak was 64; input/output
discarded counters remained zero. The 16 pre-open output rejections remained
unchanged during echo, as expected for Blink B's startup print.

The nondestructive `smoke.py --resident-version m8exp4` then passed on the
same boot: exact 16,384-byte fragmented echo, 4,096-byte slow-reader echo,
busy second client, 4,096-byte ownership-transfer echo, and 4,096-byte
fresh-session reconnect echo. All byte comparisons passed. The three sketch
discard/rejection counters were unchanged across the first two echoes;
sketch output peak rose to 128, below the 256-byte capacity. Loop boundaries
advanced from 36,543 to 37,845 during that test. Core-0 sampled heap minimum
was 164,972 bytes and sampled stack minimum was 3,920 bytes; these samples do
not prove an exhaustive memory bound.

A separate bounded 60.2-second sustained check completed 120 fresh FMGO
sessions. Each opened the channel, returned 4,096 exact bytes with a distinct
fixed seed, checked epoch acknowledgement and zero sketch input/output
discard counters, and closed. In total, 491,520 bytes matched. The boot ID
remained `c22e2e7527750092`, loop boundaries advanced from 75,141 at the
first ten-second sample to 119,413 at the final sample, and sampled heap/stack
minima remained 164,972/3,920 bytes. This test kept the host awake and does
not reproduce overnight sleep.

The user then confirmed roughly 500 ms D13 blinking, disconnected and
replugged the Nano normally, and observed continued blinking. The next hello
reported the same firmware/device identity with new boot ID
`95dc90cab17de147`. Full M8 console smoke passed again on that boot, including
all 28,672 exact bytes, second-client refusal, ownership transfer, and fresh
reconnect. The 16,384-byte echo took 0.655 s; the slow-reader echo took
0.417 s. The three sketch discard/rejection counters were unchanged;
input/output discarded remained zero and output peak reached 128. This is
one successful normal cold replug, not a repeated attachment qualification.

Read-only host route checks showed the default Internet interface was `en0`,
identified as Wi-Fi, while the Nano route used `en5`. With a channel open on
the same boot, a cache-busting HTTPS request to `https://example.com` bound to
`en0` completed with status 200 and 559 response bytes in 0.174 s while a
131,072-byte exact fragmented echo ran over the Nano connection. That echo
took 9.050 s, with no change in any of the three sketch discard/rejection
counters. No host network settings were changed. This establishes one macOS
Wi-Fi Internet coexistence observation, not iPhone/iPad routing behavior.

The user next put the Mac to sleep for five minutes with the Nano connected,
then reported D13 still blinking at approximately 500 ms per state. No board
reset or ROM entry occurred. After wake, FMGO hello retained boot ID
`95dc90cab17de147` and the expected identity. Full M8 console smoke passed
again, including all 28,672 exact bytes, busy second client, ownership transfer,
and fresh reconnect. The 16,384-byte echo took 1.100 s, and the slow-reader
echo took 0.662 s. Loop boundaries advanced from 544,951 to 546,430 across
the first two echoes. Epoch/ack both reported nine; sketch input/output
discarded remained zero, startup rejection remained 16, and output peak was
128. Sampled heap/stack minima remained 164,972/3,920 bytes. This is one
successful short host-sleep/wake cycle with no reboot; it does not reproduce
an overnight duration or a console connection held open across sleep.

Before installation, the normal native suite passed 19/19 CTest targets,
the focused sanitized session/managed-console suite passed 2/2, and the host
suite passed 169 tests. The pinned Arduino-Pico 6.0.0 resident and Blink B
module builds passed. Resident size was 132,680 flash bytes and 96,720 static
RAM bytes. These build and native results do not replace hardware evidence.

The first passing console result is consistent with the `m8exp4` reduction of
`Session::dispatch`'s compiled frame from 3,348 to 2,036 bytes. It does not
prove that stack exhaustion caused the previous failures. Actual IDE-client
reconnect, a connection held open across host sleep, and overnight behavior on
this new image have not yet been observed. M8 acceptance remains open.
