# Contributing

Read README.md, docs/BUILD.md and docs/COMPATIBILITY.md before making changes.
Project contributions are made under MIT; retain the separate licenses
and headers of third-party code.

Run `npm test` and `npm run check:repo`. For bridge changes, build with Zig
0.14.1 and run the applicable Windows native tests. State which real host,
plugin version and operating system you tested; compilation is not runtime validation.

Do not commit vendor binaries/assets, converted ZIPs, local validation caches,
credentials or machine-specific paths. Synthetic test fixtures are preferred.
If a bug requires a proprietary file, describe how to reproduce it with a user's
own installation instead of attaching the file.

Keep saved plugin IDs and state compatibility stable. New DAW backends and output
platforms must be explicitly identified in the interface and tested separately.
