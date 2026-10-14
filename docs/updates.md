# Noctra updates (1.1.0 bootstrap, 1.2.0 experimental compatibility release)

## Recipient behavior

1.0.0 ZIPs have no updater: send the new 1.1.0 ZIP once. Future copies check the public latest-release endpoint for `brobro12235/Noctraexternal` on normal launch. Checking runs on a worker thread; no GitHub credentials are shipped. Test/benchmark launches skip the network check. Misc / Version and Config / Version expose Check, Download, then Install and restart with explicit confirmation. Saving settings remains manual; unsaved settings are lost at restart.

Publishing a release is what distributes a change; editing local code does not update remote computers. Offline/closed copies cannot check until next launch. Copies in protected/nonwritable/reparse-point folders or running in multiple instances can block replacement. No elevation, service, task scheduler, installer or continuous polling is used.

Only the EXE is replaced remotely. Keep the bundled license notices; changes introducing different redistributed assets/licenses require a fresh full ZIP. No arbitrary ZIP extraction is executed on recipients. Failed startup triggers rollback; the previous EXE remains in the adjacent `Noctra-update-*` folder. That folder also contains the helper and manifest for troubleshooting and is not cleaned automatically.

## Maintainer: publish a tested release

The release utility is local-only; it creates and verifies signed assets, while GitHub release publication is performed separately.

- The generated **public** verifier key is [config/update-public-key.txt](../config/update-public-key.txt). Commit/share this with source if desired; it contains no private key.
- The private P-256 signing key is protected with Windows user DPAPI at `%LOCALAPPDATA%\NoctraPublisher\signing-key.dpapi`, outside the project/package. Do not upload it or put it in chat. Its current form is tied to this Windows user/profile; loss of that profile/key prevents signing releases trusted by existing copies. Make a secure backup/recovery plan before broad rollout. This is NOT a public certificate/Authenticode signing identity.
- Preserve the public key across releases. Rotating it without a migration means old copies reject the new signature.

For each future release, increment the project version and `NOCTRA_UPDATE_SEQUENCE` default in [CMakeLists.txt](../CMakeLists.txt). Since that value is cached, explicitly pass the new sequence during configure. Sequence 2 belongs to 1.1.0 and sequence 3 to 1.2.0; future releases need 4, 5, etc. Do not reuse a sequence/tag for a different EXE.

```bash
# Rebuild 1.2.0 only before publishing its immutable assets:
cmake -S . -B build -DNOCTRA_UPDATE_SEQUENCE=3
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release
./build/Release/noctra_release.exe sign dist/Noctra.exe 3 1.2.0 v1.2.0 365 releases/update-v1.2.0.json
```

The tool refuses to overwrite files, verifies the pinned public key, signature, target URL/hash and x64 payload before writing. Never modify the signed EXE afterward. In the public GitHub repository, create a normal stable Release with matching tag (e.g. `v1.2.0`) and attach:

1. `Noctra.exe` (exact signed-hash payload, from dist).
2. `update.json` (upload the generated version-specific manifest under this asset name).
3. The versioned share ZIP for first-time users/manual recovery.

The current 1.2.0 manifest is [releases/update-v1.2.0.json](../releases/update-v1.2.0.json), sequence 3. Publish it with tag `v1.2.0`, upload a copy named `update.json`, attach the matching current [EXE](../dist/Noctra.exe) and [1.2.0 ZIP](../releases/Noctra-1.2.0-windows-x64.zip). The historical [1.1.0 manifest](../releases/update-v1.1.0.json), sequence 2, matches the EXE inside its old ZIP, **not** the current dist EXE; do not mix them. The 1.2.0 experimental PF adapter has not been verified rendering a final live scene; disclose that limitation in release notes. Prefer immutable GitHub Releases. Keep any unsigned SHA256SUMS as a separate human integrity aid, not the updater's trust root. Offset documents are fetched separately at runtime from the configured HTTPS feed, selected by the detected client build, and cached beside the EXE; no offset data is bundled into the release manifest.

## Signed format / safety bounds

`update.json` is an envelope with exactly `payload` (a JSON string) and `signature` (128 lowercase hex characters: raw P-256 ECDSA r||s). The signature covers SHA-256 of the payload string's exact bytes, not a reserialized JSON object. The pinned key is a Windows BCRYPT_ECCPUBLIC_BLOB encoded as lowercase hex.

Payload keys: `schema`=1, `product`="Noctra", `arch`="windows-x64", `sequence` (monotonic unsigned integer), `version`, `url`, `size`, `sha256`, `expires` (Unix seconds). Require expiry in the next 366 days, a newer sequence, a repository-scoped GitHub release URL named Noctra.exe, and a Windows x64 PE payload under 32 MiB. A 32 KiB manifest and bounded HTTP responses prevent unbounded downloads. Redirects are followed explicitly only to GitHub/GitHub asset hosts over HTTPS; cookies/authentication are disabled. Unknown build support stays disabled.

The helper rechecks the signed manifest and payload, verifies the parent executable identity, waits for exit, uses same-volume ReplaceFile with a backup, then waits for the new process to confirm that it presented the menu. This is a startup check, not proof every game feature works. Sequence high-water state is in `%LOCALAPPDATA%\Noctra\update-highwater.txt`; user-local tampering/rollback is outside the protection model. This is not full TUF, offers no signed key rotation or threshold trust, and cannot defend against malicious code signed with a stolen publisher key. OS signing/reputation and another-PC testing remain separate concerns.

## References

- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases) and [release assets](https://docs.github.com/en/rest/releases/assets): public unauthenticated discovery/downloads; no private token in distributed clients.
- [GitHub rate limits](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api): unauthenticated clients share per-IP limits; one automatic check per launch, with manual retries only.
- [Windows ReplaceFile](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew) and [CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata).
- [TUF specification](https://theupdateframework.github.io/specification/latest/): stronger future metadata/key-rotation model.
