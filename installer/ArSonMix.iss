; ArSonMix x64 RC from exact independently verified FAT app, no new DSP or GUI.
#ifndef SourceRoot
  #error "Supply /DSourceRoot with SHA-verified staging directory"
#endif
#ifndef OutputRoot
  #error "Supply /DOutputRoot with destination directory"
#endif
[Setup]
AppId=ArSonMix.LiveStudio
AppName=ArSonMix
AppVersion=0.1.0-rc.1
AppVerName=ArSonMix RC (hardware FAT pending)
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
; Windows Restart Manager asks the running mixer to shut down gracefully before
; replacing its EXE. The JUCE shutdown persists the accepted mixer document.
; Never use CloseApplications=force / taskkill: unsaved state must survive.
CloseApplications=yes
CloseApplicationsFilter=ArSonMix.exe,*.dll,*.vst3
; An install owns the ONE post-setup launch; Restart Manager must not also
; restore the old process (duplicate/old-version instance race).
RestartApplications=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
DirExistsWarning=no
[Files]
Source: "{#SourceRoot}\verified-gui.exe"; DestDir: "{app}"; DestName: "ArSonMix.exe"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILD_PROVENANCE.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\FactoryFX\*"; DestDir: "{app}\FactoryFX"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
[Icons]
Name: "{autoprograms}\ArSonMix"; Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"
; Once files and shortcuts have been committed, offer a checked launch on
; the ordinary Finish screen. Never start an audio device on silent CI installs
; or background updates. Run with the original (not elevated) user context.
[Run]
Filename: "{app}\ArSonMix.exe"; Description: "Launch ArSonMix"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent runasoriginaluser
; No UninstallDelete: AppData Studio, .smix, plug-in preferences and recovery
; documents belong to the user. Do not install portable_mode.txt.
