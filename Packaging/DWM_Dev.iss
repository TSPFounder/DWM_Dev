; Inno Setup script for a DWM_Dev Windows installer.
;
; Requires Inno Setup 6 (free): https://jrsoftware.org/isdl.php
; Build with:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Packaging\DWM_Dev.iss
;
; SOURCE is the ARCHIVED build, not Saved/StagedBuilds -- the archive is what
; BuildCookRun finished with and is the thing that has actually been run.
;
; A note on size: the installer will be roughly as large as the build. The cook
; already compresses its .ucas/.pak containers, so LZMA has little left to take.
; An installer makes distribution tidy; it is not a way to get under a hosting
; limit.

#define AppName        "Dream World Maker"
#define AppVersion     "0.2"
#define AppPublisher   "TSP"
#define AppExeName     "DWM_Dev.exe"
; The archive BuildCookRun writes to. Builds_Capped was a one-off directory from
; when the texture cap was applied by hand; the cap now lives in
; DefaultDeviceProfiles.ini and applies to every cook, so the normal output is
; already capped. Pointing here at a stale directory silently ships an old build.
#define BuildDir       "C:\DreamWorldMaker\Builds\Windows"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=C:\DreamWorldMaker\Installers
OutputBaseFilename=DWM_Setup_{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
; DISK SPANNING IS REQUIRED, not optional: Inno refuses to build a single
; Setup.exe over ~4.2 GB, and this payload is ~12.4 GB. Output becomes
; DWM_Setup_x.exe plus numbered .bin volumes, which the installer reassembles.
;
; SLICE SIZE IS DELIBERATELY UNDER 2 GB. GitHub caps a release asset at 2 GB, so
; 1.9 GB slices mean every file can be uploaded to a Release -- roughly seven of
; them for this build. That is the only route found so far that puts a build this
; size on GitHub at all.
DiskSpanning=yes
DiskSliceSize=1900000000
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
; Refuses to start rather than half-installing on a full disk.
ExtraDiskSpaceRequired=0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
; Everything the archive step produced, recursively.
; EXCLUDE Saved. It holds crash dumps, minidumps and every debug log the build has
; written -- none of which belongs in someone else's install, and all of which the
; game rewrites while the installer is reading it. A log rotating mid-compile is
; what aborted the 0.2 build with "the system cannot find the file specified".
Source: "{#BuildDir}\*"; DestDir: "{app}"; Excludes: "*\Saved\*,*\Saved"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
