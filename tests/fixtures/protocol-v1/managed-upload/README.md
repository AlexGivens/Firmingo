# Experimental managed-upload vectors

FMGO protocol 1, request ID 9, synthetic device/build identities. The chunk
contains a 264-byte validation fixture bound to API address `0x1002154c`; its
code bytes are test data, not executable firmware. Never flash these fixtures.
The exact digest and both JSON/binary encodings are checked by native and host
tests. A valid finish acknowledges RAM verification, not installation.

| Request/context | Expected response fixture |
| --- | --- |
| `hello-request.hex`, unowned application channel | `hello-response.hex` (M9 target advertised) |
| `begin-request.hex`, armed | `accepted-response.hex` (zero bytes received) |
| `chunk.hex`, offset 0 | `chunk-response.hex` (264 bytes received) |
| `finish-request.hex` | `verified-response.hex` |
| `commit-request.hex` | `committing-response.hex` (queued, slot untouched) |
| `status-request.hex`, after fake flash readback | `installed-response.hex` |
| `status-request.hex`, after fake generation acknowledgement | `boot-confirmed-response.hex` |
| `abort-request.hex`, verified stage | `aborted-response.hex` (`error:none`) |
| `status-request.hex`, fresh session after owner disconnect | `disconnected-response.hex` (`error:disconnected`) |
| `status-request.hex`, fake program failure | `install-failed-response.hex` (`ok:true`, `state:failed`, slot touched) |
| `begin-request.hex`, unarmed | `unauthorized-response.hex` (`ok:false`) |

Native tests feed the requests through production Session, CapsuleStage and
SketchSwitch and compare complete response frames, with fake flash/runtime
ports. The response states are not hardware claims. See the
[SDK handoff](../../../../docs/integration/managed-sketch-sdk.md) for bundle export
and cross-project acceptance. The synthetic API address and identities are not
defaults for a real board. Existing vectors remain byte-for-byte unchanged.
