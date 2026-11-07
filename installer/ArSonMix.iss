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
CloseApplications=no
RestartApplications=no
[Files]
Source: "{#SourceRoot}\verified-gui.exe"; DestDir: "{app}"; DestName: "ArSonMix.exe"; Flags: ignoreversion
Source: "{#SourceRoot}\BUILD_PROVENANCE.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\FactoryFX\*"; DestDir: "{app}\FactoryFX"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
[Icons]
Name: "{autoprograms}\ArSonMix"; Filename: "{app}\ArSonMix.exe"; WorkingDir: "{app}"
; No Run or UninstallDelete: do not auto-activate live audio or erase
; AppData Studio. Do not install portable_mode.txt.
