; ArSonMix x64 per-user Inno Setup: safe in-place upgrade + single launch.
; Exact independently verified FAT app; no new DSP or GUI.
#ifndef SourceRoot
  #error "Supply /DSourceRoot with SHA-verified staging directory"
#endif
#ifndef OutputRoot
  #error "Supply /DOutputRoot with destination directory"
#endif
#ifndef SourceAppVersion
  #error "Supply /DSourceAppVersion from the authoritative CMake project version"
#endif
[Setup]
AppId=ArSonMix.LiveStudio
AppName=ArSonMix
AppVersion={#SourceAppVersion}
AppVerName=ArSonMix {#SourceAppVersion} RC (hardware FAT pending)
AppPublisher=ArSonMix
AppPublisherURL=https://masarray.github.io/arsonmix/
DefaultDirName={localappdata}\Programs\ArSonMix
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
WizardStyle=modern
OutputDir={#OutputRoot}
OutputBaseFilename=ArSonMix-Windows-x64-Setup-UNQUALIFIED
SetupIconFile={#SourceRoot}\arsonmix-app.ico
UninstallDisplayIcon={app}\ArSonMix.exe
Compression=lzma2
SolidCompression=yes
UsePreviousAppDir=yes
DisableWelcomePage=yes
DisableDirPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
SetupMutex=ArSonMix.PerUserInstaller.SingleInstance
; Windows Restart Manager asks the running ArSonMix to close normally,
; invoking JUCE shutdown/session persistence before any executable is replaced.
; Never force-kill an audio session or restart an old process beside the new one.
CloseApplications=yes
CloseApplicationsFilter=ArSonMix.exe
RestartApplications=no
[Files]
Source: "{#SourceRoot}\verified-gui.exe"; DestDir: "{app}"; DestName: "ArSonMix.exe"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILD_PROVENANCE.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\FactoryFX\*"; DestDir: "{app}\FactoryFX"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
[Icons]
Name: "{autoprograms}\ArSonMix"; Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"
; Do not create a second copy of an already running mixer. Restart Manager
; closes the prior verified app before [Files], while [Run] launches exactly
; one new executable only after a successful interactive install/update.
; Silent enterprise/FAT installs deliberately do not launch desktop audio.
[Run]
Filename: "{app}\\ArSonMix.exe"; WorkingDir: "{app}"; Flags: nowait runasoriginaluser skipifsilent

; Never delete private Studio data on install/update/uninstall; never install
; portable_mode.txt or silently ARM an audio output.
