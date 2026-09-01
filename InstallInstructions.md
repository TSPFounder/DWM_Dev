# Installing Dream World Maker

Step-by-step instructions for downloading and installing the game from
GitHub.

---

## Download and install (Windows)

**Everything lives on the Releases page:**

### https://github.com/TSPFounder/DWM_Dev/releases

### Before you start

| | |
|---|---|
| **System** | Windows 10 or 11, 64-bit |
| **Download** | ~12 GB |
| **Free disk space** | ~25 GB — the download and the installed game both need room |

---

### The important part

The game is too large for a single file, so it ships as **one `.exe` plus several
`.bin` volumes**. You need **all of them**, and they must end up in the **same
folder**. Downloading only the `.exe` will start an install that fails part-way
through.

---

### Option A — download them yourself

1. Open the [Releases page](https://github.com/TSPFounder/DWM_Dev/releases).
2. Click the **latest release** at the top.
3. Scroll to **Assets** and expand it if collapsed.
4. Download **every** file in the list:
   - `DWM_Setup_0.1.exe`
   - `DWM_Setup_0.1-1.bin`
   - `DWM_Setup_0.1-2.bin`
   - …and so on through the last `.bin`
5. Move all of them into **one folder** — for example `Downloads\DWM`.
6. Double-click **`DWM_Setup_0.1.exe`** and follow the installer.

Windows may warn that the publisher is unrecognised, because the installer isn't
code-signed. Choose **More info → Run anyway** if you trust the source.

---

### Option B — let a script fetch them

Less clicking, and it checks the files afterwards.

1. From the same **Assets** list, download only **`DWM_Download.ps1`**.
2. Put it in the folder you want the game files in.
3. Right-click the folder → **Open in Terminal** (or open PowerShell and `cd` there).
4. Run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File DWM_Download.ps1
   ```

5. When it finishes it prints the path to the installer. Run that.

`-ExecutionPolicy Bypass` is needed because Windows blocks downloaded scripts by
default. It applies to that single run and changes nothing on your machine.

The script downloads every file, skips any it already has, and checks each one is
complete. **If it's interrupted, just run it again** — finished files are kept and
it picks up where it stopped.

---

### If something goes wrong

**The installer stops partway with an error about a missing or corrupt file.**
One of the `.bin` volumes is missing or was cut short. Check every file from the
Assets list is in the folder, then re-download any whose size looks wrong. Option
B catches this before the installer runs.

**"Windows protected your PC".** SmartScreen, because the installer isn't
code-signed. **More info → Run anyway.**

**The script won't run.** Use the full command above, including
`-ExecutionPolicy Bypass`. Double-clicking a `.ps1` file opens it in Notepad
instead of running it.

---

## Building from source

Requires Unreal Engine 5.3 and Visual Studio 2022.

```bash
# package a Windows build
"C:\Program Files (x86)\Epic Games\UE_5.3\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="<path>\DWM_Dev.uproject" -noP4 -platform=Win64 -clientconfig=Development -cook -build -stage -pak -archive -archivedirectory="<output>"
```

```bash
# turn that build into an installer (needs Inno Setup 6)
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Packaging\DWM_Dev.iss
```

`Packaging/DWM_Dev.iss` writes the spanned installer; the slice size is kept
under 2 GB so every file fits GitHub's release-asset limit.
