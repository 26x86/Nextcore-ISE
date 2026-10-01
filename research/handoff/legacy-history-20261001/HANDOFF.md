# Historical public history handoff — nextcore-ise

## Current Status

The authoritative main at `1ed6776cf715922b07dfff84576ccca4436fd137` is the active module API. The older local public main `251fbd53b9158eceac09579a9cd1f8a9b5497cec` and frozen source HEAD `0d722886753268ec366f068823c37ea10132fb43` contain 8 commit identities absent from that initial main ancestry. Ordinary local merge commits preserve those identities without replacing the current API or dependency pins. Publication must be verified separately.

## Target State

Keep the current module implementation active. Retain the distinct older conflict variants below as historical research source with their exact bytes, original paths, Git blob identifiers and SHA-256 hashes in [manifest.json](manifest.json). The preserved variants are unfinished historical work; their presence makes no compiler, runtime, operating-system boot, Metal or device acceptance claim.

## Conflict disposition

The history merge has no active source tree changes. Existing runtime and memory provider APIs remain active.

## License

Each recorded source revision includes its original `LICENSE.txt` in the manifest. The current module [LICENSE.txt](../../../LICENSE.txt) also remains unchanged.
