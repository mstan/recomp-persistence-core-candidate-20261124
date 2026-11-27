# recomp-persistence-core

The streaming atomic-file owner also provides `recomp_atomic_file_begin_create`
and `recomp_atomic_file_create` for final-name creation without replacement.
An existing file, save, directory or symlink is never opened for writing.
Concurrent creators have exactly one winner, and readers see the complete
flushed file. Windows uses its atomic rename without the replacement flag;
POSIX uses atomic hard-link publication of private staging. Unsupported
filesystems fail without a check-then-rename fallback. The existing replacement
APIs retain their behavior. UTF-8 paths, CWD binding, limits and failed-write
rollback use the same owner. The fixture exercises both normal and older Windows
publication paths; Linux/macOS CI checks the actual POSIX filesystem operation.

Current licensing and the grant for existing owner contributions are documented in [LICENSE-SCOPE.md](LICENSE-SCOPE.md).

Private host-native persistence extraction, built without a console runtime or game inputs. It combines Retro-Runtime snapshot envelopes and identity checks with Genesis immutable asynchronous native-save writing and exclusive atomic publication. Production consumers remain unchanged.

## Build and link

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --timeout 0
```

Requires C11/C++17, threads and zlib 1.2.9 or newer. Use `add_subdirectory` and link `recomp-persistence-core::core`. Windows MinGW and the portable POSIX source paths are provided; only the recorded host/toolchain is qualified by this extraction's test evidence. No installed binary ABI is promised yet.

## Snapshot storage

`recomp_persistence/slot_store.hpp` supplies SlotStore with an explicit directory, immutable session identity, engine serializer callbacks and resource limits. The caller invokes submit/load at its own safe machine boundary. Capture and restore run on that producer thread; compression, hashing and disk publication operate on copied snapshot bytes on a worker. The service never calls a running machine from the worker. Collect each write with poll or wait before another submission/load; destruction drains an accepted write without creating a new snapshot.

Slot names are derived from bounded zero-based indices. Header-only slot listing avoids reading the state payload. Optional zlib compression is lossless; raw storage remains available. The envelope stores SHA-256 of stored bytes, decoded size and decoded SHA-256. Loading checks build/content/accessory/simulation identity, stored bytes, codec, decoded size and decoded hash before invoking engine restore. The engine owns transactional restoration of machine state if its callback fails. Unknown codecs, invalid sizes, truncated/corrupt files and mismatched identities fail before restore. Limits default to 512 MiB per snapshot and 10 slots; a consumer can supply its appropriate resource budget.

SlotStore clears optional compatibility promises and binds disposable snapshots to the current build. No old-state migration is required. The lower-level inherited `retro::state` envelope API remains available, including its explicit compatibility fields, but does not bypass SlotStore's build-specific policy.

Envelope publication streams its header, thumbnail and already-owned state bytes through one bounded file transaction, preserving the existing format without allocating a second complete envelope/state image.

RewindStore holds engine-owned snapshots within an explicit byte budget and evicts oldest complete entries. It does not serialize the machine or choose a rewind schedule. Native persistent progress is a separate API/directory contract; never use a snapshot slot path for a native save.

## Native saves and atomic files

C consumers use `recomp_persistence/native_writer.h` for the extracted fixed-size immutable writer and `recomp_persistence/atomic_file.h` for direct publication. Initial bytes describe already committed data; equal snapshots avoid redundant writes unless submit_force is requested. Acceptance is not persistence completion. A busy writer rejects another submit without replacing its pending bytes. Completion notifications run only during producer poll/wait, after a successful write. Flush accepts the latest bytes after draining prior work; destroy drains only already accepted work. C callbacks must not throw exceptions. Thread creation/resource failure can use the donor's synchronous execution behavior; no game execution fallback is introduced.

Atomic publication writes an exclusive same-directory temporary, flushes and commits it, closes it, then replaces the target. Concurrent writers have disjoint temporary names. Failed temporary writes/replacements preserve the old destination, and successful calls use the platform's documented flush/replace semantics. This is not a claim against arbitrary storage-device failure. Temporary cleanup only removes the file this operation created. UTF-8 native-save paths are supported on Windows.

Streaming serializers use `recomp_atomic_file_begin` with an inclusive byte budget, append through `recomp_atomic_file_write`, then commit or abort. Both final operations consume the handle. A failed or over-budget write is sticky and prevents publication even if the serializer ignores its error. Flush drains the staging stream; commit additionally synchronizes file data before atomic replacement. Transactions add no worker or whole-file allocation. The buffer API uses this same owner. One owner serializes calls on a transaction; independent transactions may remain open and commit to the same destination. Begin binds one absolute target and derives its staging path from that binding, so later process working-directory changes cannot redirect publication or cleanup. POSIX path components retain their original symlink/`..` resolution. Serializers and live backing-handle rebinding remain caller-owned.

On Windows, a duplicated DELETE-capable handle retains the staged file after its stream has synchronized and closed successfully. `FileRenameInfoEx` with replace/POSIX flags supports targets held by delete-sharing readers; a non-delete-sharing lock still refuses publication. Unsupported API/class results use the existing `MoveFileExW` atomic replacement after closing that handle; this fallback safely refuses an unavailable open-target replacement. No target is truncated or removed to emulate replacement. The Windows tests also execute that legacy fallback explicitly. The existing POSIX synchronized-file/rename path remains separate.

## Qualification

Synthetic tests cover inherited identity/envelope cases, compressed/raw slot roundtrips, existing-slot replacement, immutable capture, producer callback ownership, failed publication preserving old progress, concurrent atomic writers, invalid limits, corruption, truncated files, codec/decompression guards, bounded rewind and C interoperability. No game save/load, rewind behavior, runtime performance improvement or production integration is claimed. Retain native saves/settings and prior human acceptance when consumers adopt this component.

See PROVENANCE.md and the per-source license notices.
