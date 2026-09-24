# FMGO protocol/session coverage audit — 2026-09-23

## Result

The remaining M5 protocol/session test checklist is covered by tests that execute
the production portable codec, Session/Channel, Stream, application/UART backend,
and raw-lwIP adapter code. No parallel protocol implementation is used for these
cases, and this audit did not add redundant tests merely to rename existing
coverage.

The current verification run passed:

- 10/10 CTest executables with address and undefined-behavior sanitizers.
- 153/153 Python host tests, including loopback TCP harness cases.

The first host run inside the restricted command sandbox passed 135 tests and
blocked 18 loopback-server cases with `PermissionError: Operation not permitted`
at `bind(('127.0.0.1', 0))`. Repeating the same command with permission to bind
loopback passed all 153. This was an execution-environment restriction, not a
firmware or test failure.

## Requirement-to-test map

| Required behavior | Principal production-code tests |
| --- | --- |
| Binary transparency | `binary_full_duplex_survives_every_partial_write_size`; `coalesced_commands_and_all_partial_writes_preserve_binary_data`; host `test_partial_frames_full_duplex_binary_and_unsupported_control` |
| Every split of representative control frames | `every_split_preserves_empty_small_and_maximum_frames`; `all_control_frame_splits_keep_exactly_one_correlated_response` |
| Coalesced messages | `coalesced_frames_leave_tail_for_next_frame`; `coalesced_commands_and_all_partial_writes_preserve_binary_data`; `coalesced_frames_stop_receive_credit_when_peer_stalls` |
| Partial reads and writes | `binary_full_duplex_survives_every_partial_write_size`; `chained_input_partial_writes_and_memory_pressure_preserve_binary`; `full_echo_survives_send_memory_exhaustion_and_partial_writes` |
| Empty, maximum, and oversized input | `every_split_preserves_empty_small_and_maximum_frames`; `encoder_checks_bounds_before_touching_output`; `maximum_binary_frame_and_negotiated_overflow_have_correct_boundaries` |
| Malformed frames and JSON | `all_header_faults_stop_before_collecting_payload`; `strict_json_checks_syntax_unicode_numbers_and_duplicates`; `malformed_json_releases_owner_and_flushes_one_error_without_retry`; host response-header rejection cases |
| Queue saturation and slow readers | `stalled_reader_is_bounded_and_other_direction_progresses`; `stalled_output_is_bounded_while_input_progresses_and_then_drains`; `stalled_backend_bounds_input_and_allows_opposite_direction`; raw-lwIP memory-pressure cases |
| Timeout boundaries | `idle_timeout_handles_clock_rollover_and_exact_boundary`; `timeout_boundaries_cover_rollover_trickle_idle_and_error_flush`; `handshake_timeout_starts_at_accept_even_before_first_application_poll` |
| Single-owner behavior | `second_owner_cannot_steal_or_discard_pending_bytes`; `busy_connection_cannot_steal_or_discard_owner_queues`; `second_socket_receives_busy_without_displacing_owner` |
| Unsupported controls and reset acknowledgement | `unsupported_upload_open_config_and_wrong_target_have_clear_errors`; `exact_hello_and_unsupported_reset_fixtures_match_production_output`; UART configuration rejection tests |
| Reconnect cleanup and no replay | `disconnect_discards_both_queues_before_reconnect`; `disconnect_close_and_destruction_discard_queues_before_new_owner`; `callbacks_defer_backend_cleanup_and_new_owner_gets_no_replay`; host confirmed-close/reconnect cases |

Names above are defined in `tests/native/` or `tests/host/`. Hardware results
remain separate: this audit proves portable behavior and harness behavior, not a
new board, iOS FMGO interoperability, or additional physical attachment cycles.

## Commands run

```sh
.venv/bin/python tools/dev.py test --sanitize
```

The successful run used the same command with loopback TCP binding allowed. No
board was flashed or contacted.
