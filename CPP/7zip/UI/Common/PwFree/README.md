# PwFree - automatic archive password recovery

7zFM, 7zG and the console builds of this fork ship an optional step that runs
before the password dialog: when an archive turns out to be encrypted, PwFree
tries to recover the password on its own and only asks the user when it fails.

The feature is off the critical path. If anything inside the module goes wrong
- no probe for the format, network down, corrupt dictionary - the code falls
back to the stock behaviour and the normal password dialog appears.

## Where the entry point is

All four UI implementations of `IOpenCallbackUI::Open_CryptoGetTextPassword`
consult the module before showing a dialog:

| target | file | class |
| --- | --- | --- |
| 7zFM.exe | `CPP/7zip/UI/FileManager/OpenCallback.cpp` | `COpenArchiveCallback` |
| 7zFM.exe (extract) | `CPP/7zip/UI/FileManager/ExtractCallback.cpp` | `CExtractCallbackImp` |
| 7zG.exe | `CPP/7zip/UI/GUI/UpdateCallbackGUI.cpp` | `CUpdateCallbackGUI` |
| 7z.exe / 7za / 7zz / 7zr | `CPP/7zip/UI/Console/OpenCallbackConsole.cpp` | `COpenCallbackConsole` |

Each class carries an `ArcPath` member that the caller fills in right after the
archive path is known (`FileFolderPluginOpen.cpp`, `List.cpp`, `Main.cpp`,
`ExtractCallbackConsole.cpp`, `UpdateGUI.cpp`). The hook itself is three lines
and is guarded by `#ifdef Z7_PWFREE`.

Everything below them lives in `CPP/7zip/UI/Common/PwFree` and is independent
of the archive handlers: it parses ZIP headers itself and asks the caller for
nothing but the path.

## Chain

`NPwFree::Resolve` builds a verifier for the archive and walks the providers in
priority order. A provider is a pull-style iterator (`Next(UString&)`), so the
budget and the cancellation checks sit in the single loop that drives the chain
and no provider can forget to yield.

1. **seed** - the password the caller already had (from the command line or a
   previous dialog).
2. **cache** - passwords recovered earlier in the same process, keyed by size,
   modification time and the tail of the path.
3. **dict** - the vendor online dictionary: ZIP archives of base64 lines, each
   line a DES-ECB block encrypted with a fixed key, PKCS#5 padded. Six
   dictionaries are tried in descending usefulness; the payload is kept in
   memory only.
4. **cloud** - `yun.php` and `checkFile.php` keyed by the archive MD5. The
   answer is treated as a candidate list, never as an answer: a password taken
   from the network has to pass the same local verification as one from the
   dictionary.

Each provider has a budget (3 s for the cloud lookup, 120 s for the dictionaries
by default, both configurable) and the loop aborts as soon as one candidate
verifies.

## Verification

A candidate is accepted only when it decrypts and passes a real integrity check:

* **ZipCrypto** - the 12 byte encryption header is verified first (8 bits), then
  the first entry is decrypted completely and its CRC32 is compared. The header
  alone is not enough: about one in 256 wrong passwords passes it.
* **WinZip AES** - PBKDF2-HMAC-SHA1 with 1000 iterations, compared against the
  2 byte password verifier and then the 10 byte HMAC of the payload.
* **7z, encrypted header** (`-mhe=on`) - the packed next header is decrypted,
  decoded through the coder chain (AES, then LZMA, LZMA2 or Copy) and its CRC32
  is compared with the digest stored in the folder record. A header that stores
  no digest has to parse cleanly as a real header instead.
* **7z, encrypted payload** - the first AES folder is probed: the AES output
  size, the zero padding of the last block and, when the folder stores a CRC,
  the decoded CRC32 are checked. Without a digest the LZMA2 chunk chain behind
  the AES coder is walked and every chunk has to land exactly on the AES output
  boundary. A random key passed neither test in 300 000 trials.
* Any other format returns `E_PWFREE_NOPROBE` and the module steps aside.

Self extracting archives are handled by both probes: the 7z signature is looked
up in the first 16 MiB of the file and every offset the header stores is
measured from it, while a ZIP stub shifts the central directory by the distance
between the end record and the offset it records. An archive therefore only has
to be readable, not to start at byte zero.

The 7z probe reads the signature, the start header and the next header only;
even an archive with a huge payload costs a single KDF run per candidate. The
key derivation is the 7-Zip SHA-256 loop over 2^19 salt/password/counter records
and runs through bcrypt, roughly 7 ms per candidate on one core.

## Settings

Stored under `HKCU\Software\7-Zip-Zstandard\PwFree`:

| value | default | meaning |
| --- | --- | --- |
| `Enabled` | 1 | run the chain at all |
| `UseCloud` | 1 | allow the online lookup |
| `UseLocalDict` | 1 | allow the online dictionaries |
| `ShowSource` | 1 | report where a password came from |
| `CloudBudgetMs` | 3000 | budget of the cloud provider |
| `DictBudgetMs` | 120000 | budget of the dictionary provider |

The checkboxes at the bottom of the 7zFM settings page toggle `Enabled`,
`UseCloud`, `UseLocalDict` and `ShowSource`; the last one makes the recovered
password report its source ("PwFree: password from downloaded dictionary") in
the status line of 7zFM and 7zG and on the error stream of the console build.

## Build

`CPP/7zip/UI/Common/PwFree/PwFree.mk` is included by every target that links
the facade and does three things:

* defines `Z7_PWFREE`, which enables the hooks;
* appends the twelve objects to `PWFREE_OBJS`;
* adds the LZMA and LZMA2 decoders only to the targets that do not link them yet;
* adds `bcrypt.lib` and `winhttp.lib`.

`7zip.mak` compiles those objects with `$(COMPL_O1)`, that is without a
precompiled header, because the targets sharing the module use different PCH
contents. No other part of the build system changed, and nothing in
`CPP/7zip/Archive` or `CPP/7zip/Crypto` was touched.

`.github/workflows/build-windows.yml` builds x64 and ARM64 with
`build-it.cmd -all` and uploads the bundles as artifacts.