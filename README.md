# recomp-persistence-core

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

RewindStore holds engine-owned snapshots within an explicit byte budget and evicts oldest complete entries. It does not serialize the machine or choose a rewind schedule. Native persistent progress is a separate API/directory contract; never use a snapshot slot path for a native save.

## Native saves and atomic files

C consumers use `recomp_persistence/native_writer.h` for the extracted fixed-size immutable writer and `recomp_persistence/atomic_file.h` for direct publication. Initial bytes describe already committed data; equal snapshots avoid redundant writes unless submit_force is requested. Acceptance is not persistence completion. A busy writer rejects another submit without replacing its pending bytes. Completion notifications run only during producer poll/wait, after a successful write. Flush accepts the latest bytes after draining prior work; destroy drains only already accepted work. C callbacks must not throw exceptions. Thread creation/resource failure can use the donor's synchronous execution behavior; no game execution fallback is introduced.

Atomic publication writes an exclusive same-directory temporary, flushes and commits it, closes it, then replaces the target. Concurrent writers have disjoint temporary names. Failed temporary writes/replacements preserve the old destination, and successful calls use the platform's documented flush/replace semantics. This is not a claim against arbitrary storage-device failure. Temporary cleanup only removes the file this operation created. UTF-8 native-save paths are supported on Windows.

## Qualification

Synthetic tests cover inherited identity/envelope cases, compressed/raw slot roundtrips, existing-slot replacement, immutable capture, producer callback ownership, failed publication preserving old progress, concurrent atomic writers, invalid limits, corruption, truncated files, codec/decompression guards, bounded rewind and C interoperability. No game save/load, rewind behavior, runtime performance improvement or production integration is claimed. Retain native saves/settings and prior human acceptance when consumers adopt this component.

See PROVENANCE.md and the per-source license notices.
