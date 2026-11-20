# GGPO SDK

Native GGPO SDK sources and public header from
[pond3r/ggpo PR #81](https://github.com/pond3r/ggpo/pull/81), revision
`4fe81167d904d95bfdce09857504938fd764931f`, with the POSIX build support.
See [LICENSE](LICENSE) for the MIT terms.

The engine builds the native SDK as `libggpo.a` on Linux. Local changes
propagate state callback failures and sync-test mismatches, release sync-test
checkpoints, expose the confirmed input frame, and validate incoming UDP
packet sizes and compressed input. Socket bind errors reach the caller.
Set `GGPO_LOG=1` if you need the SDK's verbose Linux log.
