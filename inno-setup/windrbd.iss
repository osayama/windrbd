; Script generated by the Inno Setup Script Wizard.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING INNO SETUP SCRIPT FILES!
; Use iscc to compile this (in your innosetup program directory)

; Autogenerated by versioninfo.sh: contains MyAppVersion
#include "version.iss"

; Windows-style path to the root of the source distribution
; You probably have to change this.
; Update: those are now in Makefile.win in the BUILD_ENVironments
; #define WindrbdSource "X:\windrbd"
; Where the WinDRBD utils (drbdadm, drbdsetup, drbdmeta and windrbd EXEs)
; can be found. Note: the utils are not built by this makefile, you
; have to build them seperately.
; #define WindrbdUtilsSource "X:\drbd-utils-windows"

#define MyAppName "WinDRBD"
#define MyAppPublisher "Linbit"
#define MyAppURL "http://www.linbit.com/"
#define MyAppURLDocumentation "https://downloads.linbit.com/"
#define SysRoot "C:\windrbd"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{EB75FCBA-83D5-4DBF-9047-30F2B6C72DC9}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
ChangesEnvironment=yes
DefaultDirName={pf}\{#MyAppName}
DisableDirPage=yes
DefaultGroupName={#MyAppName}
LicenseFile={#WindrbdSource}\LICENSE.txt
InfoBeforeFile={#WindrbdSource}\inno-setup\about-windrbd.txt
OutputDir={#WindrbdSource}\inno-setup
OutputBaseFilename=install-{#MyAppVersion}
PrivilegesRequired=admin
Compression=lzma
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
SetupIconFile=windrbd.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "greek"; MessagesFile: "compiler:Languages\Greek.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "scottishgaelic"; MessagesFile: "compiler:Languages\ScottishGaelic.isl"
Name: "serbiancyrillic"; MessagesFile: "compiler:Languages\SerbianCyrillic.isl"
Name: "serbianlatin"; MessagesFile: "compiler:Languages\SerbianLatin.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
; Name: modifypath; Description: &Add application directory to your environmental path;

[Dirs]
Name: "{#SysRoot}\usr\sbin"
Name: "{#SysRoot}\var\run\drbd"
Name: "{#SysRoot}\var\lib\drbd"
Name: "{#SysRoot}\var\lock"

[Files]
Source: "{#WindrbdUtilsSource}\user\v9\drbdadm.exe"; DestDir: "{#SysRoot}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdmeta.exe"; DestDir: "{#SysRoot}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\v9\drbdsetup.exe"; DestDir: "{#SysRoot}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdUtilsSource}\user\windrbd\windrbd.exe"; DestDir: "{#SysRoot}\usr\sbin"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\sysroot\README-windrbd.txt"; DestDir: "{#SysRoot}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.conf"; DestDir: "{#SysRoot}\etc"; Flags: ignoreversion onlyifdoesntexist
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.d\global_common.conf"; DestDir: "{#SysRoot}\etc\drbd.d"; Flags: ignoreversion onlyifdoesntexist
Source: "{#WindrbdSource}\inno-setup\sysroot\etc\drbd.d\windrbd-sample.res"; DestDir: "{#SysRoot}\etc\drbd.d"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\uninstall-windrbd-beta4.cmd"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\install-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
Source: "{#WindrbdSource}\inno-setup\uninstall-windrbd.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygwin1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\cygwin-binaries\cygrunsrv.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\converted-sources\drbd\windrbd.sys"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\inno-setup\msgbox.vbs"; DestDir: "{app}"; Flags: ignoreversion deleteafterinstall
; must be in same folder as the sysfile.
Source: "{#WindrbdSource}\converted-sources\drbd\windrbd.inf"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\converted-sources\drbd\windrbd.cat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\misc\drbd.cgi"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WindrbdSource}\misc\ipxe-windrbd.pxe"; DestDir: "{app}"; Flags: ignoreversion

; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Icons]
Name: "{group}\{cm:ProgramOnTheWeb,{#MyAppName}}"; Filename: "{#MyAppURL}"
Name: "{group}\View {#MyAppName} Tech Guides"; Filename: "{#MyAppURLDocumentation}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{group}\Open {#MyAppName} configuration folder"; Filename: "C:\windrbd\etc\drbd.d"
Name: "{group}\Open {#MyAppName} application folder"; Filename: "{app}"
                                                
[Run]
Filename: "{app}\uninstall-windrbd-beta4.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser shellexec waituntilterminated runhidden
Filename: "C:\Windows\sysnative\cmd.exe"; Parameters: "/c install-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden
Filename: "C:\windrbd\usr\sbin\windrbd.exe"; Parameters: "install-bus-device windrbd.inf"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden; Check: DoCreateBusDevice
Filename: "{#MyAppURLDocumentation}"; Description: "Download WinDRBD documentation"; Flags: postinstall shellexec

[UninstallRun]
Filename: "C:\Windows\sysnative\cmd.exe"; Parameters: "/c uninstall-windrbd.cmd"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden; RunOnceId: "UninstallWinDRBD"
Filename: "C:\windrbd\usr\sbin\windrbd.exe"; Parameters: "remove-bus-device windrbd.inf"; WorkingDir: "{app}"; Flags: runascurrentuser waituntilterminated shellexec runhidden; RunOnceId: "RemoveBusDeviceWinDRBD"

[Code]
const
	ModPathType = 'system';

	function ModPathDir(): TArrayOfString;
	begin
		setArrayLength(Result, 2);
		Result[0] := ExpandConstant('{app}');
		Result[1] := ExpandConstant('C:\windrbd\usr\sbin');
	end;

#include "modpath.iss"

function UninstallNeedRestart(): Boolean;
begin
	Result := True;
end;

#include "oldver.iss"

#include "services.iss"

var LoggerWasStarted: boolean;
var UmHelperWasStarted: boolean;

Procedure StopUserModeServices;
Begin
	LoggerWasStarted := MyStopService('windrbdlog');
	UmHelperWasStarted := MyStopService('windrbdumhelper');
End;

Procedure StartUserModeServices;
Begin
	if LoggerWasStarted then begin
		MyStartService('windrbdlog');
	End;
	if UmHelperWasStarted then begin
		MyStartService('windrbdumhelper');
	End;
End;

procedure CurStepChanged(CurStep: TSetupStep);
begin
	if CurStep = ssPostInstall then begin
		ModPath();
	end;

	if CurStep = ssInstall then begin
		StopUserModeServices();
	end;
	if CurStep = ssPostInstall then begin
		StartUserModeServices();
	end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
	if CurUninstallStep = usAppMutexCheck then begin
		StopUserModeServices();
	end;
	// only run during actual uninstall
	if CurUninstallStep = usUninstall then begin
		ModPath();
	end;
	if CurUninstallStep = usDone then begin
		MsgBox('Uninstall does not remove the C:\Windrbd directory, since it may contain files modified by you. If you do not need them any more, please remove the C:\Windrbd directory manually.', mbInformation, MB_OK);
	end;

	// cmd script stops user mode helpers, no need to do that here
end;

var InstallBusDeviceCheckBox: TNewCheckBox;  

procedure InitializeWizard;
var  
	InstallBusDeviceLabel1: TLabel;  
	InstallBusDeviceLabel2: TLabel;  
	InstallBusDeviceLabel3: TLabel;  
	InstallBusDeviceLabel4: TLabel;  
	InstallBusDeviceLabel5: TLabel;  

	MainPage: TWizardPage;  

begin
	MainPage := CreateCustomPage(wpSelectTasks, 'Options', 'Please select installation options below.');
	InstallBusDeviceLabel1 := TLabel.Create(MainPage);
	InstallBusDeviceLabel1.Parent := MainPage.Surface;
	InstallBusDeviceLabel1.Top := 0;
	InstallBusDeviceLabel1.Left := 0;
	InstallBusDeviceLabel1.Caption := 'In order to support disk devices and boot devices, a virtual SCSI bus device has';

	InstallBusDeviceLabel2 := TLabel.Create(MainPage);
	InstallBusDeviceLabel2.Parent := MainPage.Surface;
	InstallBusDeviceLabel2.Top := InstallBusDeviceLabel1.Top + InstallBusDeviceLabel1.Height;
	InstallBusDeviceLabel2.Left := 0;
	InstallBusDeviceLabel2.Caption := 'to be installed on the system. This installer can do this, please uncheck this';

	InstallBusDeviceLabel3 := TLabel.Create(MainPage);
	InstallBusDeviceLabel3.Parent := MainPage.Surface;
	InstallBusDeviceLabel3.Top := InstallBusDeviceLabel2.Top + InstallBusDeviceLabel2.Height;
	InstallBusDeviceLabel3.Left := 0;
	InstallBusDeviceLabel3.Caption := 'checkbox if automatic creation of the bus device causes troubles. You can';

	InstallBusDeviceLabel4 := TLabel.Create(MainPage);
	InstallBusDeviceLabel4.Parent := MainPage.Surface;
	InstallBusDeviceLabel4.Top := InstallBusDeviceLabel3.Top + InstallBusDeviceLabel3.Height;
	InstallBusDeviceLabel4.Left := 0;
	InstallBusDeviceLabel4.Caption := 'always create the bus device later with device manager (see the tech guide on';

	InstallBusDeviceLabel5 := TLabel.Create(MainPage);
	InstallBusDeviceLabel5.Parent := MainPage.Surface;
	InstallBusDeviceLabel5.Top := InstallBusDeviceLabel4.Top + InstallBusDeviceLabel4.Height;
	InstallBusDeviceLabel5.Left := 0;
	InstallBusDeviceLabel5.Caption := 'the boot device for a howto).';

	InstallBusDeviceCheckBox := TNewCheckBox.Create(MainPage);
	InstallBusDeviceCheckBox.Parent := MainPage.Surface;
	InstallBusDeviceCheckBox.Top := InstallBusDeviceLabel5.Top + InstallBusDeviceLabel5.Height + 8;
	InstallBusDeviceCheckBox.Left := 0;
	InstallBusDeviceCheckBox.Width := 100;
	InstallBusDeviceCheckBox.Caption := 'Install bus device';
	InstallBusDeviceCheckBox.Checked := true;
end;

function DoCreateBusDevice: Boolean;
begin
	Result := InstallBusDeviceCheckBox.Checked;
end;
