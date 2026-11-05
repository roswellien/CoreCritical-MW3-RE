; The Windows installer for players (docs/windows.md, Packaging): Inno Setup
; (jrsoftware.org, free), built by tools\windows\package.cmd from the
; package folder it has just made:
;   iscc /DAppVersion=<git describe> /DDist=<the package folder> /O<out dir> tools\windows\installer.iss
; It installs to Program Files\MechWarrior 3 Rebuild with a Start-menu
; shortcut (a desktop one ticked by default) and an uninstaller. The game
; writes nothing there: the player's settings, saves, imported movies, music
; and a disc install live in %APPDATA%\MicroProse\MechWarrior 3, which the
; uninstaller leaves alone. No file of the original game is in the installer.

#ifndef AppVersion
  #define AppVersion "dev"
#endif
#ifndef Dist
  #error Give /DDist=<the package folder> (package.cmd does).
#endif

[Setup]
; The same AppId every version: a new installer upgrades the old install in place.
AppId={{920C98A6-550D-41A3-839E-540B93B8F40D}
AppName=MechWarrior 3 Rebuild
AppVersion={#AppVersion}
AppVerName=MechWarrior 3 Rebuild {#AppVersion}
AppPublisher=MechWarrior 3 Rebuild (a fan project)
AppComments=A rebuild of MechWarrior 3's program; it needs your own copy of the game.
DefaultDirName={autopf}\MechWarrior 3 Rebuild
; The folder page every time, an upgrade's too (the default hides it there),
; offering the folder already installed to.
DisableDirPage=no
DefaultGroupName=MechWarrior 3 Rebuild
DisableProgramGroupPage=yes
; Program Files needs administrator rights; a player without them can pick
; "only for me" (the user's own Programs folder) in the first dialog.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=mw3.ico
UninstallDisplayIcon={app}\mw3.exe
UninstallDisplayName=MechWarrior 3 Rebuild
OutputBaseFilename=MW3-Rebuild-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
#if FileExists(AddBackslash(Dist) + "LICENSE.txt")
LicenseFile={#Dist}\LICENSE.txt
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; The launcher is the shortcut, named for the game; mw3.exe is installed
; and starts alone too, but has no shortcut (Owen, 2026-11-02; launcher.md).
Name: "{autoprograms}\MechWarrior 3"; Filename: "{app}\mw3launcher.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\MechWarrior 3 Rebuild - Read me"; Filename: "{app}\README.txt"
Name: "{autodesktop}\MechWarrior 3"; Filename: "{app}\mw3launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\mw3launcher.exe"; Description: "{cm:LaunchProgram,MechWarrior 3}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only what the installer put in {app}; %APPDATA%\MicroProse\MechWarrior 3 stays.
Type: dirifempty; Name: "{app}"

[Messages]
UninstalledAll=%1 was removed from your computer.%n%nYour settings, pilots and saved games, and any MechWarrior 3 installed from your disc, are kept in %APPDATA%\MicroProse\MechWarrior 3. Delete that folder to remove them too.
