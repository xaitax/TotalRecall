# TotalRecall Reloaded

**Breaking Windows Recall. Again.**

<img width="697" height="1024" alt="image" src="https://github.com/user-attachments/assets/adb8e67b-29f2-4376-b94b-7a2e2e9d5655" />

When Microsoft [redesigned Recall](https://blogs.windows.com/windowsexperience/2024/09/27/update-on-recall-security-and-privacy-architecture/) with VBS enclaves, AES-256-GCM encryption, Windows Hello authentication, and a Protected Process Light host, the message was clear: the data is locked in a vault.

The vault is solid. The delivery truck is not.

`AIXHost.exe`, the process that renders the Recall timeline, has no PPL, no AppContainer, no code integrity enforcement. Any process running as the logged-in user can inject code into it and call the same COM APIs the legitimate UI uses. Once the user authenticates with Windows Hello, decrypted screenshots, OCR text, and metadata flow through `AIXHost.exe` as live COM objects. TotalRecall Reloaded sits inside that process and extracts everything.

**No admin required. Standard user. No kernel exploit. No crypto bypass. Just COM calls.**

---

## How It Works

### The Injection

TotalRecall Reloaded is two files: an injector (`totalrecall.exe`) and a payload DLL (`totalrecall_payload.dll`).

The injector finds `AIXHost.exe` via `CreateToolhelp32Snapshot`, allocates memory in the target with `VirtualAllocEx`, writes the DLL path with `WriteProcessMemory`, and spawns a remote thread pointing at `LoadLibraryW`. Classic DLL injection. Nothing fancy, because nothing fancy is needed. `AIXHost.exe` has zero protections against it.

This works from **standard user** privilege. No elevation, no `SeDebugPrivilege`. The default Windows DACL allows same-user processes full access to each other. Verified: the token runs at Medium mandatory level with `BUILTIN\Administrators` set to deny-only.

### Authentication

The VBS enclave won't decrypt anything without Windows Hello. The tool doesn't bypass that. It makes the user do it, silently rides along when the user does it, or waits for the user to do it.

`--launch` simulates **Win+J** via `keybd_event`, the keyboard shortcut that opens the Recall timeline. The user sees a Hello prompt (face, fingerprint, or PIN), authenticates, and the enclave starts serving decrypted data. From the user's perspective, Recall just opened normally. From ours, the payload is already inside, waiting.

`--stealth` is the fully silent mode. It works like this:

1. Injects into `AIXHost.exe` (always running) and patches `DiscardDataAccess` to a no-op
2. Waits silently for the user to open Recall and authenticate normally
3. When the user closes Recall, Baker.dll tries to revoke the data access grant, but the patch blocks it
4. `AIXHost.exe` dies and respawns. The tool detects the restart and re-injects into the new process
5. The auth grant persists in `aihost.exe` (revocation was blocked). Extraction begins immediately
6. No Win+J, no Hello prompt, no visible UI. Up to 5 re-injection attempts.

`--wait` is the passive counterpart to `--launch`. Instead of simulating Win+J, the tool sits idle while the user opens Recall on their own — from the taskbar, a shortcut, or any other path. When `AIXHost.exe` appears and the user completes Hello naturally, the payload is injected and extraction begins. Useful on a machine being observed, or when the Recall session needs to look entirely user-initiated with no synthetic keyboard input.

### The Extraction Chain

Once inside `AIXHost.exe`, the payload initializes a COM apartment with `CoInitializeEx(COINIT_APARTMENTTHREADED)` and sets up proxy identity forwarding with `CoSetProxyBlanket(EOAC_DYNAMIC_CLOAKING)`. This is critical. Without dynamic cloaking, the COM proxy doesn't carry the authenticated identity to the server.

The extraction follows the same path the legitimate Recall UI uses:

1. **Enclave initialization**: `DataManager.Load()` triggers enclave key loading. `DataStoreManager.DecryptDatabase()` (slot 37) prepares decrypted views. The payload polls `DataManager.DataStatus` until it returns 3 (unlocked).

2. **Entity enumeration**: `MemoryEntityStatics.GetLightMemoryItemsBefore()` (slot 9) returns a vector of lightweight entity references. Each carries a context ID at offset +8. On a typical machine, this returns hundreds of entities spanning days or weeks of activity.

3. **Per-entity extraction**: For each context ID, the payload loads the full entity via `ContextEngine2.TryGetEntityForId()` (slot 6), unwraps it through `IEntityWrapper` (slot 6), and `QueryInterface` to `IMemoryEntity`. From there:

   - **Metadata** (synchronous): title (slot 8), app model ID (slot 9), app name (slot 10), process path (slot 11), URL (slot 12), domain (slot 13), file URI (slot 15), timestamp (slot 7), window bounds (slot 16). Via QI to IMemoryEntity2/3: restore capability, app dwell time, web dwell time
   - **Screenshot** (async): `TryGetBitmapCaptureAsync()` (slot 19) returns a `SoftwareBitmap`. `QueryInterface` to `ISoftwareBitmapNative`, call `GetData(IID_IWICBitmap)` to get a WIC bitmap, encode as PNG via `IWICBitmapEncoder`
   - **OCR + NER + AI** (async): `ContextEngine2.TryGetMemoryEntityDetailsForIdAsync()` (slot 8) returns entity details. QI to `IMemoryEntityDetails` for `OcrLines` (slot 7), `IMemoryEntityDetails2` for NER text entities (people, emails, addresses), and `IMemoryEntityDetails4` for AI activity descriptions

4. **Retry rounds**: Baker.dll (the Recall UI library) populates the ContextEngine cache asynchronously. After the initial pass, the payload pumps Windows messages for 3 seconds (`PeekMessage`/`DispatchMessage` loop) and retries any entities that weren't available. Each round typically yields ~12 additional entities. Up to 10 retry rounds.

Every call is wrapped in `__try/__except` because a single access violation on a COM proxy call permanently kills the RPC channel to `aihost.exe`. There's no recovery. You'd have to restart `AIXHost.exe`. The SEH wrappers catch crashes from wrong parameter types and keep the session alive.

### Pre-Auth Capabilities

Several operations work without any Hello authentication:

**Screenshot Extraction**: `RecallPrivacyIndicatorSettings` (CLSID `{42C63551-...}`) exposes `GetRecentCaptureThumbnail(width, height)` at slot 13. The method name says "thumbnail" but the server doesn't enforce a resolution cap. Passing 3840x3840 returns the most recent Recall capture at full resolution. The `IRandomAccessStream` result is converted to an `IStream` via `CreateStreamOverRandomAccessStream` (shcore.dll) and dumped as BMP.

**Data Destruction**: `IDataStoreManager::DeleteEvents()` (slot 12) wipes the entire capture history. No parameters, no authentication. Ghidra analysis confirmed: the delete handler at `FUN_1802ddd10` contains zero calls to the authorization gate function. The auth check was never wired in.

**Metadata Disclosure**: Storage paths (including the user-specific UKP GUID), database size, retention policy, capture state, and the most recent capture context ID are all readable without auth via `IDataStoreManagerStatics` and `RecallPrivacyIndicatorSettings`.

---

## Usage

<img width="1825" height="1418" alt="image" src="https://github.com/user-attachments/assets/f181b5a6-2af6-4070-a8cb-6d294f1cdfc4" />


```
totalrecall.exe --launch             Open Recall, trigger Hello, extract everything
totalrecall.exe --stealth            Silent extraction (patches auth revocation, waits)
totalrecall.exe --wait               Wait for user to manually open Recall
totalrecall.exe --preauth            Grab latest screenshot + settings (no Hello)
totalrecall.exe --search "password"  Search OCR text in latest extraction
totalrecall.exe --destroy            Wipe all Recall data (confirmation required, no Hello)
```

| Mode | Auth Required | What It Does |
|------|:---:|-------------|
| `--launch` | Hello | Simulate Win+J, user authenticates, full extraction |
| `--stealth` | Passive | Patches auth revocation, waits for user to authenticate Recall, extracts silently |
| `--wait` | Hello | Wait for user to open Recall naturally, then extract |
| `--preauth` | **No** | Latest screenshot + all settings |
| `--search` | No | Case-insensitive OCR text search across latest extraction |
| `--destroy` | **No** | `DeleteEvents()`, irreversible, requires typing DESTROY to confirm |

### Example Output

**`--stealth` (first run, waiting for user):**
```
[+] Target: AIXHost.exe  PID 8648 (stealth mode)
[*] Patching auth revocation...
[+] Waiting for Recall session...
[+] Recall session detected
[*] Waiting for user to close Recall...
[*] Recall closed, waiting for AIXHost to respawn...
[+] Extracting from AIXHost PID 26208
[+] Payload active
[*] Extracting
[##############################] 384/384 entities

EXTRACTION COMPLETE  6 min 51 sec
Screenshots       192   328.8 MB
OCR Text          184   535.2 KB
Metadata (CSV)    384    97.4 KB
```

**`--stealth` (subsequent run, cached session):**
```
[+] Target: AIXHost.exe  PID 27532 (stealth mode)
[*] Patching auth revocation...
[+] Waiting for Recall session...
[+] Cached session found, extracting...
[*] Extracting
[##############################] 398/398 entities
```

**`--launch`:**
```
[+] Target: AIXHost.exe  PID 14636  Memory 60 MB
[*] Triggering Recall via Win+J...
[*] Waiting for Hello authentication authenticating
[+] Recall ready  PID 14636  Memory 242 MB
[*] Extracting
[##############################] 212/212 entities

EXTRACTION COMPLETE  3 min 59 sec
Screenshots       104   184.0 MB
```

**`--preauth` (no Hello required):**
```
[+] Target: AIXHost.exe  PID 27532 (pre-auth mode)
[*] Injecting payload (pre-auth only)...
[+] Payload active

PRE-AUTH EXTRACTION COMPLETE  0 min 1 sec

Screenshot  4K (3840x2464)  36.1 MB

Settings
  Storage Path    C:\Users\<user>\AppData\Local\CoreAIPlatform.00\UKP\{...}
  Storage Size    178.3 MB
  Capture Count   0
  Retention Days  90
```

**`--search`:**
```
Searching for: "password"
In: extraction_20260406_152736

[1] === [12] ctxId=90443 Settings | Chrome ===
     Saved passwords and passkeys
[2] === [47] ctxId=91201 inbox | Thunderbird ===
     Your temporary password has been reset
```

### Output Directory

```
extraction_20260404_143052/
    screenshots/              Full-resolution decrypted PNGs
    screenshots/*.txt         Per-image OCR text
    thumbnails/               Thumbnail PNGs (fallback when full screenshot unavailable)
    ocr_text.txt              Combined OCR text for all captures
    recall_data.csv           Structured metadata
    settings.txt              Storage path, size, retention, capture state
    latest_capture_4k.bmp     Pre-auth screenshot of most recent capture
    extraction.log            Detailed extraction log with timing
```

## Building

**Requirements:** Visual Studio with ARM64 C++ tools, Windows 11 ARM64 with Recall enabled.

```
make.bat
```

Produces `totalrecall.exe` and `totalrecall_payload.dll`. Both must be in the same directory when run.

---

## Recall's Trust Model

```
VTL1 (Secure World)
+--------------------------------------------------+
|  VBS Enclave: AES-256-GCM, sealed keys           |
|  snapshot_support.dll / storage_support.dll       |
|  Keys never leave here. Crypto is sound.          |
+--------------------------------------------------+
               ^ CallEnclave
               |
VTL0 (Normal World)
+--------------------------------------------------+
|  aihost.exe (PPL, Signer=5)                      |
|  +-- Microsoft.Windows.AI.Platform.dll (6.9 MB)  |
|      44 methods on IDataStoreManager alone        |
|      Enclave bridge. Protected. Can't touch it.   |
|                                                   |
|  AIXHost.exe (NO PROTECTION)                      |
|  +-- Baker.dll: OCR, NER, AI classification       |
|  +-- Receives decrypted data for rendering        |
|  +-- CreateRemoteThread = game over               |
+--------------------------------------------------+
```

The key hierarchy: Hello -> NGC ECDH P-384 (TPM-backed) -> VTL1 mutual auth -> enclave-sealed key material -> per-page AES-256-GCM with random nonces and page-number AAD. Six layers of key derivation. The cryptography is genuinely solid.

The problem is what happens after decryption. The plaintext crosses into `AIXHost.exe`, an unprotected, injectable, same-user process. The enclave doesn't distinguish between `Baker.dll` and injected code. It can't.

---

## Key Findings

### The Trust Boundary Ends Too Early

Microsoft's [architecture blog](https://blogs.windows.com/windowsexperience/2024/09/27/update-on-recall-security-and-privacy-architecture/) states that "processes outside the VBS Enclaves never directly receive access to snapshots or encryption keys" and that the design "restricts attempts by latent malware trying to ride along with a user authentication to steal data."

In practice, `AIXHost.exe` receives every decrypted screenshot and OCR result as a live COM object. There's no per-caller verification inside the process. No "are you Baker.dll?" check. If you're in the process, you're trusted. The security boundary is the VBS enclave and the PPL, not the rendering process. Decrypted data is one `CreateRemoteThread` away from any same-user application.

### The IResponse4 Access Control Bypass

A direct authorization gap.

When you call `ContextDataSource.Search()` and get back an `IResponse`, the natural path is `IResponse.get_Items()` (slot 9) to get the results. On a fresh session, this returns `0x80005473`, a custom Recall-specific error code. The server is deliberately rejecting the call. `IResponse2.ItemsAfterIndex()` returns the same error. Access control is working.

But the `IResponse` object implements four interface versions. `IResponse4.get_UnfilledItems()` (slot 9 on a different IID) returns the same underlying data collection with no access check at all.

```
IResponse.get_Items()           -> 0x80005473 (ACCESS DENIED)
IResponse2.ItemsAfterIndex()    -> 0x80005473 (ACCESS DENIED)
IResponse4.get_UnfilledItems()  -> S_OK (all entities returned)
```

Same data. Different interface version. No authorization. The method was intended for internal lazy-loading in the search pipeline. The security review caught `get_Items` and `ItemsAfterIndex`, but missed `get_UnfilledItems`. This is the pattern that makes security engineering hard: the check exists on one code path (which means someone decided it was needed), and is missing on another.

From those items, each entity's context ID leads to `ContextEngine2.TryGetEntityForId()`, which loads the full entity with screenshots, OCR, and metadata. The enclave decrypts everything on request.

### The DiscardDataAccess Bypass

When the user closes the Recall window, Baker.dll calls `IDataProtectionManager3::DiscardDataAccess()` to explicitly revoke the data access grant in `aihost.exe`. This is why the auth doesn't persist after the user's normal Recall session: Baker.dll cleans up after itself.

The bypass: injected code inside `AIXHost.exe` can patch the COM proxy vtable to replace `DiscardDataAccess` (slot 8) with a no-op function. One `VirtualProtect` call, one pointer write. When the user closes Recall, Baker.dll calls the patched slot, nothing happens, and the grant remains active in `aihost.exe`. Any subsequent `AIXHost.exe` instance inherits the cached grant silently.

In practice, `--stealth` deploys the patch into `AIXHost.exe` (which is always running) and waits. The next time the user opens and closes Recall normally, the cleanup is suppressed and the data access grant persists. The tool detects access, handles AIXHost process restarts via automatic re-injection (re-patching each new instance), and extracts everything silently. The patch is per-process and per-run: it protects the current extraction session. Subsequent Recall usage after the tool exits reverts to normal behavior.

### DeleteEvents: Destruction Without Auth

`IDataStoreManager::DeleteEvents()` wipes the entire capture history without Windows Hello. Ghidra confirmed: the delete handler contains zero calls to the authorization gate function. The auth check was never wired into the delete path. An attacker who can't read the data can still destroy it. Anti-forensics from standard user.

### Pre-Auth Screenshot Extraction

`RecallPrivacyIndicatorSettings.GetRecentCaptureThumbnail` returns the most recent Recall screenshot at whatever resolution you request. The method is intended for the small privacy indicator in the taskbar. Nobody capped the resolution. Any same-user process can silently grab what was last on screen, no Hello required.

### Pre-Auth Capture Counting via GetSecureStorageInfo

`GetWindowCaptureCount` (slot 26) returns E_ACCESSDENIED without Hello. But `GetSecureStorageInfo` (slot 27) returns a `StorageInfo` struct with the exact same data, no auth required. The struct contains `NumberOfItems` (capture count) and `Size` (total encrypted storage in bytes). An attacker can monitor this to track Recall activity in real time without any authentication.

### Auth State Persistence

Once Hello is completed, the auth state is cached in `aihost.exe` (PPL) for the entire Windows session. Killing and restarting `AIXHost.exe` does not clear it. An attacker can wait for the user to naturally open Recall, then silently extract data hours later. Unlimited re-extraction with no additional prompts, no visible windows, no user awareness.

---

## What Recall Captures (and What Gets Extracted)

Recall doesn't just take screenshots. It builds a comprehensive behavioral profile of everything you do on your computer. Every few seconds, it captures a screenshot, runs OCR and (supposedly) AI classification on it, and stores the result in an encrypted SQLite database.

Everything below is confirmed from two independent sources: the private WinRT metadata (`Microsoft.Windows.AI.Platform.winmd`, parsed via `cppwinrt.exe`) and the VBS enclave binary (`storage_support.dll`, which contains the complete database schema in plaintext strings).

### Per-Capture Data

_Note: the field names below are confirmed from the WinRT metadata and enclave binary strings. The descriptions of what each field contains are inferred from the API names and types, and have not all been dynamically verified at runtime._

| Data | Source | What It Contains |
|------|--------|------------------|
| **Screenshot** | `TryGetBitmapCaptureAsync` | Full-resolution PNG of your entire screen |
| **Resized Screenshot** | `TryGetBitmapCaptureAsync(Size, Mode)` | Screenshot at any requested resolution with configurable interpolation |
| **Thumbnail** | `TryGetBitmapCaptureThumbnailAsync` | Lower-resolution preview image |
| **OCR Text** | `OcrText` (Details3) | Full concatenated OCR of everything visible on screen |
| **OCR Lines** | `OcrLines` (Details1) | Individual OCR lines as separate strings |
| **OCR Words** | `OcrWord` struct | Every single word with pixel-precise bounding box (`RectInt32`) |
| **Window Title** | `get_Title` | Title bar text of the active window |
| **Application** | `get_AppDisplayName` | Which app was in focus (Chrome, Outlook, Terminal, ...) |
| **App Model ID** | `get_AppUserModelId` | UWP/Win32 application identity string |
| **Process Path** | `get_ProcessPath` | Full executable path (`C:\Program Files\...\chrome.exe`) |
| **App Icon** | `IMemoryEntity2.IconUri` | Path to the application's icon |
| **URL** | `get_WebUrl` | Complete URL in the browser address bar |
| **Domain** | `get_WebDomain` | Website domain |
| **Favicon** | `get_WebIconUri` | Favicon URL for the active website |
| **File Path** | `get_FileUri` | Active document or file path |
| **Window Position** | `get_WindowBounds` | Pixel-precise screen coordinates: X, Y, Width, Height |
| **Timestamp** | `get_Timestamp` | Exact capture time (100-nanosecond precision) |
| **App Dwell Time** | `IMemoryEntity3` | How long you spent in each application (milliseconds) |
| **Web Dwell Time** | `IMemoryEntity3` | How long you spent on each website (milliseconds) |
| **Sensitivity Label** | `IMemoryEntity5` | Microsoft Purview/DLP classification: name, color, tooltip |
| **User Activity ID** | `IMemoryEntity6` | Windows Timeline activity correlation ID |
| **Restore Capability** | `IMemoryEntity2` | Bitmask: can re-launch app (0x1), URL (0x2), file (0x4), URI (0x8), timeline (0x10) |
| **Context Restore** | `TryRestoreContextAsync` | Re-open the exact app, page, or document from any capture |

### AI-Processed Metadata

_Note: same caveat as above. The class and enum names are enumerated from the WinRT metadata and enclave DLL strings; the descriptions of what each represents are inferred from names and context and have not been dynamically verified for every entry._

On top of the raw capture, Recall runs AI classification that produces:

- **Named Entity Recognition** (10 types): `PersonName`, `Organization`, `Product`, `Address`, `Location`, `DateTime`, `Event`, `Duration`, `WebUrl`, `EmailAddress`. Each extracted from OCR text with source position.
- **Topic Classification** (23 categories): `Topic`, `Person`, `Emoji`, `App`, `FileKind`, `Domain`, `UserTag`, `Organization`, `Product`, `Address`, `Location`, `DateTime`, `Event`, `Duration`, `MemoryDsc`, `SensitivityLabel`, `WebVideo`, `Meeting`, `Chat`, `MailingPackage`, `TextRatio`, `SkipTopics`, `Any`. Each with confidence scores and optional bounding boxes.
- **Screen Regions** (10 types): `Text`, `Image`, `Table`, `Container`, `Menu`, `ToolBar`, `AddressBar`, `Toolpane`, `TabBar`, `TitleBar`. Each with pixel-precise bounding boxes and embedded OCR text.
- **Activity Descriptions**: `L1Description` (AI-generated prose summary of what you were doing), `L1Activity` (categorized: browsing, coding, writing, reading email), `L1Application` (AI-classified application context).
- **Memory Nodes** (4 types): `Meeting` (conference calls), `Chat` (messaging), `WebVideo` (video playback), `MailingPackage` (email/newsletters). Groups multiple captures into logical activity sessions.
- **Context Classification** (7 types): `UserActivity`, `KMeansCluster`, `LobeTopicCluster`, `ApplicationDwellTime`, `WebsiteDwellTime`, `ClipboardImageCopied`, `Topic`.
- **Sensitive Content Filtering** (186 rules, toggleable): The Platform DLL embeds Microsoft's Content Exposure Prevention (CEP) rule set as XML, used to filter sensitive content from captures. The rules cover credit card numbers, social security numbers, bank routing numbers, driver's licenses, passports, and tax IDs across 30+ countries, plus cloud credentials: Azure Storage keys, AWS S3 secret keys, Azure DevOps PATs, Azure AD client tokens, X.509 private keys, API keys, general passwords, and login credentials. The filtering is controlled via `IsContentFilteringEnabled` on the `IAutomatedCaptureController6` interface.
- **Sensitivity Labels**: Microsoft Purview/DLP classification per capture and per entity detail, with name, color, and tooltip.
- **Semantic Embeddings** (8 variants): 4 image embeddings (Original, Largest Region, Second Largest, Composite) and 4 text embeddings (Full OCR, Region-filtered, Per-region, Window Title). Stored in DiskANN vector indexes for similarity search across your entire history.

### Capture Pipeline

Every few seconds, Recall's capture service evaluates 12 policies before deciding whether to screenshot:

`GameModeActive`, `BatterySaverActive`, `UserActivityIdle`, `UserPresenceIdle`, `StorageLow`, `PrivateWindow`, `BlockedByContentProtection`, `BlockedAppId`, `BlockedExecutable`, `BlockedURL`, `BlockedContentFilePath`, `BitLockerDisabled`

If none of these trigger, it captures. The per-capture input structure (from WinRT metadata):

```
WindowData {
    WindowId, Foreground, Title, Bounds, Minimized, PrivateState, InputScopePrivacy
    AppData {
        AppUserModelId, ProcessPath, IconUri, AppName, TileId
        RemoteClient, IsBrowserWindow
    }
    RestoreData {
        WebUrl, FilePath, ActivationUri, ActivityId, WebIconUri
        FileObjectId, VolumeId       // NTFS persistent file identifiers
        SensitivityLabelData { State, Labels }
    }
}
```

### The Database

The main database (`ukg.db`) uses SQLite SEE with AES-256-GCM encryption. Schema confirmed from `storage_support.dll` (the VBS enclave binary that contains the CREATE TABLE statements in plaintext):

**Core tables (17):**

```
WindowCapture              Id, Name, ImageToken, IsForeground, WindowId, WindowBounds,
                           WindowTitle, Properties, IsProcessed, Retry, ActivationUri,
                           ActivityId, FallbackUri, TimeStamp, DwellTime
WindowCaptureAppRelation   WindowCaptureId, AppId, IsBackground
WindowCaptureWebRelation   WindowCaptureId, WebId, IsBackground
WindowCaptureFileRelation  WindowCaptureId, FileId
WindowCaptureTopicRelation WindowCaptureId, TopicId, Score (float)
WindowCaptureTextIndex     FTS5 virtual table (WindowCaptureId, WindowTitle, OcrText)
App                        Id, WindowsAppId, IconUri, Name, Path, TileId, Properties
Web                        Id, Domain, Uri, IconUri, Properties
File                       Id, Path, Name, Extension, Kind, Type, ObjectId, VolumeId
Topic                      Id, Title, Properties
ScreenRegion               Id, WindowCaptureId, RegionKind, OcrText, Bounds
AppDwellTime               Id, WindowsAppId, HourOfDay, DayOfWeek, HourStartTimestamp, DwellTime
WebDomainDwellTime         Domain, HourOfDay, DayOfWeek, HourStartTimestamp, DwellTime
SearchHistory              SessionId, CorrelationId, TimeStamp, Kind, Text, Language
SearchFeedback             SessionId, CorrelationId, TimeStamp, Kind, Text, Language,
                           ItemChosenEventId, FeedbackType
IdTable                    NextId
_MigrationMetadata         Id, Version
```

**Semantic search index (SemanticTextStore.sidb / SemanticImageStore.sidb):**

```
si_items                   Core embedding storage
si_embedding_metadata      Embedding type and source mapping
si_diskann_graph           DiskANN approximate nearest neighbor graph
si_diskann_references      Graph edge references
si_diskann_config          Index configuration
si_diskann_info            Index statistics
si_application_values      Application-level settings
```

Over a typical workday, hundreds of captures accumulate. Default retention is 90 days with a 75 GB storage threshold. Every email opened, every document edited, every website visited, every terminal command on screen, every messaging conversation visible. All OCR-processed, entity-extracted, topic-classified, and semantically indexed.

Your entire digital life, indexed and searchable. As intended.

---

## What Microsoft Got Right

The VBS enclave is rock solid. Key material never leaves VTL1. The per-page AES-256-GCM with random nonces is textbook correct. `aihost.exe` PPL protection is effective, the kernel blocks injection. CFG is comprehensive on ARM64. SQL queries are fully parameterized (ten injection payloads, zero side effects). The authentication model is stateless and race-free (thousands of probes, zero bypasses).

The fundamental problem isn't the crypto, the enclave, the authentication, or the PPL. It's sending decrypted content to an unprotected process for rendering. The vault door is titanium. The wall next to it is drywall.

---

## Responsible Disclosure

This research was responsibly disclosed to the Microsoft Security Response Center (MSRC).

### Timeline

| Date | Event |
|------|-------|
| 2024-06-07 | Original [TotalRecall](https://github.com/xaitax/TotalRecall) released (pre-encryption Recall) |
| 2024-06-13 | Microsoft delays Recall launch, announces redesign with VBS enclaves |
| 2025-04 | Recall re-launches with VBS enclaves, encryption, Hello auth |
| 2026-03-06 | MSRC report submitted: full writeup, source code, build instructions |
| 2026-03-09 | MSRC opens Case 109586, status: Review / Repro |
| 2026-03-27 | MSRC: "engineering team is currently in final stage of investigating" |
| 2026-04-03 | MSRC closes case as **Not a Vulnerability**: "operates within current documented security design" |
| 2026-04-09 | TotalRecall Reloaded public release |

### Microsoft's Position

After review with their engineering teams, MSRC determined that "the behavior observed operates within the current, documented security design of Recall" and that "the access patterns demonstrated are consistent with intended protections and existing controls." They cited their [architecture blog](https://blogs.windows.com/windowsexperience/2024/09/27/update-on-recall-security-and-privacy-architecture/), specifically that authorization "restricts attempts by latent malware trying to ride along with a user authentication to steal data" and that "processes outside the VBS Enclaves never directly receive access to snapshots or encryption keys and only receive data returned from the enclave after authorization."

The case was closed as Not a Vulnerability.

The pre-auth findings documented here (unauthenticated data destruction, screenshot extraction) were discovered during follow-up research after the case closure.

---

## Tested Environment

| | Details |
|---|---|
| **OS** | Windows 11 25H2 (Build 26300.8155) |
| **Architecture** | ARM64 |
| **AIXHost.exe** | v2126.7602.0.0 |
| **Privilege** | Standard user (Medium integrity, no elevation) |

---

## Previous Work

- [TotalRecall](https://github.com/xaitax/TotalRecall) (June 2024), the original Python tool for pre-encryption Recall
- [Kevin Beaumont's analysis](https://doublepulsar.com/recall-stealing-everything-youve-ever-typed-or-viewed-on-your-own-windows-pc-is-now-possible-da3e12e9465e), the research that started it all

## Acknowledgments

Thanks to [Jeff McJunkin](https://x.com/jeffmcjunkin) and [Kevin Beaumont](https://cyberplace.social/@GossiTheDog) for testing and validation.

---

**Alexander Hagenah ([@xaitax](https://x.com/xaitax))**
