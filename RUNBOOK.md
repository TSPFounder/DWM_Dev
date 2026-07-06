# RUNBOOK.md — DWM_Dev Build, Export & Open Procedures

The exact sequence to build, export, and run the DWMStudio → SQLite → UE pipeline, plus
recovery procedures for every failure mode hit so far. When something breaks, check the
Failure Modes table before debugging from scratch — most of these cost hours the first time
and minutes the second.

**Environment:** Windows 11 · UE 5.3.2 (Launcher install, `C:\Program Files\Epic Games\UE_5.3`)
· .NET 8 · MATLAB R2025b · Repos at `C:\DreamWorldMaker\Repos\` (`DWM_Dev`, `DWMStudio`).
Smart App Control: OFF (was blocking UBT-compiled DLLs; note: re-enabling requires a Windows
reinstall — leave off on this dev machine).

---

## 1. The Core Loop (happy path)

1. **Pre-flight:** run `preflight.ps1` (checks plugins, SDK, processes, paths). Fix anything red.
2. **Build DWMStudio** (.NET): open `DWMStudio.slnx` → build, or `dotnet build` in the repo.
3. **Export the world package** from DWMStudio → produces the `.db`.
4. **CLOSE DWMSTUDIO COMPLETELY.** It holds the SQLite file handle after export; UE gets
   `disk I/O error` on `FSQLiteDatabase::Open` if DWMStudio is still running. Non-negotiable.
5. **Open DWM_Dev in UE 5.3** (from Visual Studio's green ▶ with DWM_Dev as startup project,
   or the `.uproject`).
6. **PIE** → the reader opens the `.db` in `OnStart()` and spawns actors from package data.

## 2. Building DWM_Dev (UE side) in Visual Studio

- Open `DWM_Dev.sln`. Config **Development Editor / Win64**. Startup project **DWM_Dev**
  (under Games — right-click → *Set as Startup Project*; if the toolbar says
  "UnrealBuildTool," the green button runs the wrong thing and prints a help screen).
- **Right-click DWM_Dev → Build** (or Rebuild). Do NOT "Build Solution" — Epic's bundled C#
  tooling projects (EpicGames.OIDC, BuildGraph.Automation, AutomationScripts) fail with
  NuGet/framework errors on this machine. **Those errors are engine tooling, not ours —
  ignore them.** Only the `2>` DWM_Dev section of the Build Output matters.
- **Beware "Target is up to date" in ~0.5s** — that is a fake success (nothing compiled).
  If in doubt, use **Rebuild**, which forces a full compile.
- Success = `Binaries\Win64\UnrealEditor-DWM_Dev.dll` (+ `.pdb`, `DWM_DevEditor.target`,
  `UnrealEditor.modules`) with a fresh timestamp. (`UnrealEditor.modules` is the CORRECT
  name — it is a per-platform manifest, not per-module.)

## 3. Clean Rebuild Procedure (when builds/loads act haunted)

1. Close the editor AND Visual Studio. Check the system tray for lingering UE processes.
2. Delete from `DWM_Dev\`: `Binaries`, `Intermediate` (add `Saved`, `DerivedDataCache`, `.vs`
   for the full nuke — all regenerable, all gitignored).
3. Regenerate project files (right-click `.uproject` → *Generate Visual Studio project
   files*; if that menu item is missing, run):
   ```
   "C:\Program Files\Epic Games\UE_5.3\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="C:\DreamWorldMaker\Repos\DWM_Dev\DWM_Dev.uproject" -game -engine -progress
   ```
   (`GenerateProjectFiles.bat` does NOT exist in Launcher installs.)
   To restore the right-click menu permanently:
   ```
   "C:\Program Files\Epic Games\UE_5.3\Engine\Binaries\Win64\UnrealVersionSelector.exe" /fileassociations
   ```
4. Open the fresh `.sln` → Rebuild DWM_Dev → launch from VS.
5. Note: after deleting `.vs`, the solution shows "(not found)" projects until step 3 runs.

## 4. Failure Modes (in the order they were earned)

| Symptom | Cause | Fix |
| --- | --- | --- |
| "The game module 'DWM_Dev' could not be loaded" + log spams `Looked in:` engine folders | A module in Build.cs whose **plugin isn't enabled in the .uproject** (this was SQLiteCore/SQLiteSupport). Builds fine, won't load. | Every Build.cs dependency provided by a plugin must have an `"Enabled": true` entry in the `.uproject` Plugins array. |
| Build "succeeds" in 0.5s, nothing changes | "Target is up to date" — no compile happened | Rebuild (not Build); or delete Binaries+Intermediate first |
| `UnrealBuildTool.dll not found` / MSB3073 exit code -1 | Broken/incomplete engine install | Epic Launcher → UE 5.3 → **Verify** (quiet; done when button returns to "Launch") |
| `Application Control policy has blocked this file (0x800711C7)` on MarketplaceRules.dll etc. | Windows Smart App Control blocking UBT's freshly-compiled rules DLLs | Turn Smart App Control OFF (one-way switch) + reboot; delete `C:\Users\henry\AppData\Local\UnrealEngine\Intermediate\Build\BuildRules\` so the blocked DLLs recompile |
| `disk I/O error` on FSQLiteDatabase::Open | DWMStudio still holds the .db handle | Close DWMStudio fully before UE touches the file |
| "Missing or built with a different engine version: DWM_Dev — rebuild now?" | DLL/.modules hash mismatch (stale artifacts) | Yes is safe; if it loops, do the Clean Rebuild Procedure |
| Editor exits after a long-idle module dialog | The dialog just sat open (it blocks; timestamps in the log jump hours) | Nothing is hung "in the background" — close it and fix the underlying cause |
| OIDC / BuildGraph / AutomationScripts errors in the Error List | Epic's bundled C# tooling, broken NuGet refs | Ignore. Filter Error List to "Current Project (DWM_Dev)" |
| Right-click "Generate Visual Studio project files" missing | Lost `.uproject` file association | UBT `-projectfiles` command above; `/fileassociations` to restore the menu |

**Escalation order when stuck:** full project clean (§3) → search `Saved\Logs\DWM_Dev.log`
for the FIRST `DWM_Dev`/`Failed`/`Fatal` mention (not the bottom) → post log + Binaries
listing to the Unreal community → engine reinstall LAST (the engine is rarely the problem
once it builds).

## 5. SQLite Conventions (the ledger path)

- Plugins: engine built-in **SQLiteCore + SQLiteSupport** (both in `.uproject` AND Build.cs).
  `USQLite` marketplace plugin stays **disabled**.
- Open in **OnStart()** override, never `GameInstance::Init()` (`GetWorld()` is null pre-level).
- Open **ReadOnly** with an absolute `FPaths::…` path; log the resolved path.
- **By-name binding everywhere**: `TEXT("$col")`. Never by index — bind is **1-based**,
  column read is **0-based** (verified in 5.3 headers); mixing them corrupts silently.
- Check every `Step()` against Row/Done/Error; log `GetLastError()` on failure. The
  network-sum-zero invariant depends on no unchecked calls.
- **Packaging (before Week 9):** add to `DefaultGame.ini`:
  `+DirectoriesToAlwaysStageAsNonUFS=(Path="Databases")` — the .db does not auto-package
  in 5.3 and Open fails *silently* in shipped builds.

## 6. MATLAB Co-Simulation Notes (R2025b ↔ UE 5.3)

- R2025b requires **UE 5.3** for co-sim — this is why the engine version is frozen.
- MathWorks plugins install to `UE_5.3\Plugins\Marketplace\Mathworks`; once wired, the
  project opens **from MATLAB/Simulink**, not by double-clicking the `.uproject`.
- Known bug: R2025b **Update 2** plugin-version mismatch ("Incompatible version of 3D
  Simulation engine: 25.1.0"). If co-sim fails with that error, it's MathWorks' bug, not ours.
- Simscape STL visual paths are **relative** and break when a model moves — fix inside each
  body subsystem.
- Company licensing: the founder's MATLAB **Home license does not cover business use**;
  MathWorks **Startup program** application must precede campaign use of MATLAB-derived
  content (see business plan §4.4).

## 7. Housekeeping

- Log build-system incidents in AGENT_LOG.md; scope-affecting decisions in SCOPE.md's log.
- `UE_Library5_7` was an obsolete repo (deleted). If any tool "finds" a 5.7 project, it is
  working in the wrong place — the real sandbox is `DWM_Dev` on 5.3.
