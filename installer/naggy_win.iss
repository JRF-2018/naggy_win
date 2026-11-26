; naggy_win installer (Inno Setup 6.3 or later).
; Build: installer\build_installer.bat  (after building the binaries into
; installer\stage\; see BUILD.md)

#define AppName "naggy_win"
#define AppVersion "0.3.7"
#define AppPublisher "JRF"
#define AppURL "https://github.com/JRF-2018/naggy_win"
#define Stage "stage"

[Setup]
AppId={{1D0F4B39-3A8B-4B7E-9C70-5A6E2B0D6C11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#Stage}\LICENSE.txt
OutputDir=Output
OutputBaseFilename=naggy_win-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x86compatible x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile={#Stage}\naggy.ico
UninstallDisplayIcon={app}\naggy.ico
MinVersion=10.0
CloseApplications=no
RestartIfNeededByRun=no

[Languages]
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
; 64-bit Windows
Source: "{#Stage}\amd64\naggy-backend.exe"; DestDir: "{app}"; Check: Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\amd64\naggy-server.exe"; DestDir: "{app}"; Check: Is64BitInstallMode; Flags: ignoreversion restartreplace
Source: "{#Stage}\amd64\naggy-client.exe"; DestDir: "{app}"; Check: Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\amd64\naggy-dictool.exe"; DestDir: "{app}"; Check: Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\amd64\naggy-config.exe"; DestDir: "{app}"; Check: Is64BitInstallMode; Flags: ignoreversion
; The TSF DLLs go to the system directory (as CorvusSKK does): web
; browsers and security software trust it.
Source: "{#Stage}\x64\naggy_ime.dll"; DestDir: "{sys}\naggy_win"; Check: Is64BitInstallMode; Flags: ignoreversion regserver 64bit restartreplace uninsrestartdelete
; 32-bit (also for 32-bit applications on 64-bit Windows)
Source: "{#Stage}\386\naggy-backend.exe"; DestDir: "{app}"; Check: not Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\386\naggy-server.exe"; DestDir: "{app}"; Check: not Is64BitInstallMode; Flags: ignoreversion restartreplace
Source: "{#Stage}\386\naggy-client.exe"; DestDir: "{app}"; Check: not Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\386\naggy-dictool.exe"; DestDir: "{app}"; Check: not Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\386\naggy-config.exe"; DestDir: "{app}"; Check: not Is64BitInstallMode; Flags: ignoreversion
Source: "{#Stage}\x86\naggy_ime.dll"; DestDir: "{syswow64}\naggy_win"; Flags: ignoreversion regserver 32bit restartreplace uninsrestartdelete
; data
Source: "{#Stage}\nginit\*"; DestDir: "{app}\nginit"; Flags: ignoreversion recursesubdirs
Source: "{#Stage}\trl\*"; DestDir: "{app}\trl"; Flags: ignoreversion recursesubdirs
Source: "{#Stage}\dic\bushu-skk-dic.txt"; DestDir: "{app}\dic"; Flags: ignoreversion
Source: "{#Stage}\naggy-server.ini.sample"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\naggy.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Registry]
Root: HKLM; Subkey: "Software\naggy_win"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
; start naggy-server at logon (it shares the dictionaries among all applications)
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "naggy-server"; ValueData: """{app}\naggy-server.exe"""; Flags: uninsdeletevalue

[Icons]
Name: "{group}\naggy_win の設定"; Filename: "{app}\naggy-config.exe"
Name: "{group}\README"; Filename: "{app}\README.md"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"

[UninstallRun]
Filename: "{app}\naggy-dictool.exe"; Parameters: "disable-tip"; Flags: runhidden; RunOnceId: "DisableTip"
Filename: "{sys}\taskkill.exe"; Parameters: "/f /im naggy-server.exe"; Flags: runhidden; RunOnceId: "KillServer"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\dic"
Type: files; Name: "{app}\site-init.nginit"
Type: files; Name: "{app}\site-init.nginit.new"
Type: files; Name: "{app}\setup-dic.log"

[CustomMessages]
ja.DicPageCaption=単漢字辞書の選択
ja.DicPageDesc=変換に使う単漢字辞書を選んでください。
ja.DicPageText=naggy_win は単漢字変換 IME『風』のやり方を受け継いだ IME です。単漢字辞書は次のどちらか(または無し)を選べます。%n%n● jrf_tankanji-2026: JRF が同人的に作成した単漢字辞書です(GitHub からダウンロードします)。作成時に『風』の辞書も参考にした部分があります。作者(JRF)はダウンロードと利用を許可していますが、『風』が購入できる場合は、(おためしを超えて)使うなら『風』を購入してほしい、とのことです。%n%n● 『風』の辞書: 『風』の利用権をお持ちの方向けです。インストール済みの『風』の Wind2.rea / Wind2.dic (または変換済みの Wind2.txt)を変換して使います。辞書ファイルはこのインストーラには含まれません。
ja.DicJrf=jrf_tankanji-2026 を使う (推奨・ダウンロード)
ja.DicKaze=『風』の辞書を使う (『風』の利用権が必要)
ja.DicNone=単漢字辞書を使わない (単語変換のみ)
ja.DicKeep=辞書と設定を変更しない (再インストール用)
ja.KazePageCaption=『風』の辞書の場所
ja.KazePageDesc=Wind2.rea と Wind2.dic (または Wind2.txt) のあるフォルダを指定してください。
ja.KazePageText=『風』をインストールしたフォルダ(例: C:\Windows や C:\ProgramData\Wind2)を指定してください。Vector から入手した Wind230.exe の場合は、先にそれを実行して展開したフォルダを指定してください。
ja.OptPageCaption=追加の辞書
ja.OptPageDesc=単語変換・しぼり込みに使う辞書を選んでください(インターネットからダウンロードします)。
ja.OptPageText=SKK 辞書は GPL で配布されています。絵文字辞書は MIT License です。部首辞書(@き などで部首から漢字を引く)は常にインストールされます。
ja.OptSKK=SKK-JISYO.L (単語変換・しぼり込み検索用。強く推奨)
ja.OptEmoji=emoji-skk-dic (@@ねこ などで絵文字を入力)
ja.OptCT=SKK-JISYO.china_taiwan (中国・台湾の地名)
ja.Preparing=辞書を準備しています(ダウンロードと変換)...
ja.DicFailed=辞書の準備に失敗しました。%n%n%1%n%nあとでインストール先の naggy-dictool.exe setup を実行し直すこともできます。ログ: %2
ja.KazeNotFound=指定されたフォルダに Wind2.rea / Wind2.dic (または Wind2.txt) が見つかりません。
en.DicPageCaption=Tankanji dictionary
en.DicPageDesc=Choose the tankanji (single kanji) dictionary.
en.DicPageText=naggy_win is an input method following the tankanji conversion of the IME "Kaze".%n%n* jrf_tankanji-2026: made by JRF (downloaded from GitHub). Parts of it referred to the Kaze dictionary. The author permits its use, but asks you to buy Kaze if it can be bought and you use this dictionary beyond a trial.%n%n* Kaze dictionary: for owners of Kaze. Converts installed Wind2.rea / Wind2.dic (or Wind2.txt). Not included in this installer.
en.DicJrf=Use jrf_tankanji-2026 (recommended, download)
en.DicKaze=Use the Kaze dictionary (requires a Kaze license)
en.DicNone=No tankanji dictionary (word conversion only)
en.DicKeep=Keep the current dictionaries and settings
en.KazePageCaption=Location of the Kaze dictionary
en.KazePageDesc=Folder that contains Wind2.rea and Wind2.dic (or Wind2.txt).
en.KazePageText=Specify the folder where Kaze is installed (e.g. C:\Windows or C:\ProgramData\Wind2). For Wind230.exe, run it first and specify the extracted folder.
en.OptPageCaption=Additional dictionaries
en.OptPageDesc=Dictionaries for word conversion and filtering (downloaded).
en.OptPageText=SKK dictionaries are distributed under the GPL; the emoji dictionary under the MIT License. The radical dictionary is always installed.
en.OptSKK=SKK-JISYO.L (word conversion and filtering; strongly recommended)
en.OptEmoji=emoji-skk-dic (emoji by @@yomi)
en.OptCT=SKK-JISYO.china_taiwan (place names of China and Taiwan)
en.Preparing=Preparing dictionaries (download and conversion)...
en.DicFailed=Failed to prepare dictionaries.%n%n%1%n%nYou can run naggy-dictool.exe setup in the installation folder later. Log: %2
en.KazeNotFound=Wind2.rea / Wind2.dic (or Wind2.txt) were not found in the folder.

[Code]
var
  DicPage: TInputOptionWizardPage;
  KazePage: TInputDirWizardPage;
  OptPage: TInputOptionWizardPage;

function FindKazeDir(): String;
var
  Dirs: array of String;
  I: Integer;
begin
  Result := '';
  SetArrayLength(Dirs, 5);
  Dirs[0] := ExpandConstant('{win}');
  Dirs[1] := ExpandConstant('{commonappdata}\Wind2');
  Dirs[2] := ExpandConstant('{commonpf32}\Wind2');
  Dirs[3] := ExpandConstant('{commonpf32}\Wind');
  Dirs[4] := ExpandConstant('{commonpf}\Wind2');
  for I := 0 to GetArrayLength(Dirs) - 1 do
    if FileExists(AddBackslash(Dirs[I]) + 'Wind2.rea') and FileExists(AddBackslash(Dirs[I]) + 'Wind2.dic') then
    begin
      Result := Dirs[I];
      Exit;
    end;
end;

procedure InitializeWizard();
var
  Kaze: String;
begin
  DicPage := CreateInputOptionPage(wpSelectDir, CustomMessage('DicPageCaption'),
    CustomMessage('DicPageDesc'), CustomMessage('DicPageText'), True, False);
  DicPage.Add(CustomMessage('DicJrf'));
  DicPage.Add(CustomMessage('DicKaze'));
  DicPage.Add(CustomMessage('DicNone'));
  DicPage.Add(CustomMessage('DicKeep'));
  Kaze := FindKazeDir();
  if Kaze <> '' then
    DicPage.SelectedValueIndex := 1
  else
    DicPage.SelectedValueIndex := 0;

  KazePage := CreateInputDirPage(DicPage.ID, CustomMessage('KazePageCaption'),
    CustomMessage('KazePageDesc'), CustomMessage('KazePageText'), False, '');
  KazePage.Add('');
  KazePage.Values[0] := Kaze;

  OptPage := CreateInputOptionPage(KazePage.ID, CustomMessage('OptPageCaption'),
    CustomMessage('OptPageDesc'), CustomMessage('OptPageText'), False, False);
  OptPage.Add(CustomMessage('OptSKK'));
  OptPage.Add(CustomMessage('OptEmoji'));
  OptPage.Add(CustomMessage('OptCT'));
  OptPage.Values[0] := True;
  OptPage.Values[1] := True;
  OptPage.Values[2] := True;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = KazePage.ID) and (DicPage.SelectedValueIndex <> 1) then
    Result := True;
  if (PageID = OptPage.ID) and (DicPage.SelectedValueIndex = 3) then
    Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  D: String;
begin
  Result := True;
  if CurPageID = KazePage.ID then
  begin
    D := AddBackslash(KazePage.Values[0]);
    if not ((FileExists(D + 'Wind2.rea') and FileExists(D + 'Wind2.dic')) or FileExists(D + 'Wind2.txt')) then
    begin
      MsgBox(CustomMessage('KazeNotFound'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure PrepareDictionaries();
var
  Params, Log, Tk: String;
  Code: Integer;
  Lines: TArrayOfString;
  Msg: String;
  I: Integer;
begin
  case DicPage.SelectedValueIndex of
    0: Tk := 'jrf';
    1: Tk := 'kaze';
    2: Tk := 'none';
  else
    Exit;
  end;
  Log := ExpandConstant('{app}\setup-dic.log');
  Params := '/c ""' + ExpandConstant('{app}\naggy-dictool.exe') + '" setup --force --dest "' +
    ExpandConstant('{app}') + '" --tankanji ' + Tk;
  if Tk = 'kaze' then
    Params := Params + ' --kaze-dir "' + RemoveBackslashUnlessRoot(KazePage.Values[0]) + '"';
  if not OptPage.Values[0] then Params := Params + ' --no-skk';
  if not OptPage.Values[1] then Params := Params + ' --no-emoji';
  if not OptPage.Values[2] then Params := Params + ' --no-china-taiwan';
  Params := Params + ' > "' + Log + '" 2>&1"';
  WizardForm.StatusLabel.Caption := CustomMessage('Preparing');
  if not Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
  begin
    Msg := '';
    if LoadStringsFromFile(Log, Lines) then
      for I := 0 to GetArrayLength(Lines) - 1 do
        Msg := Msg + Lines[I] + #13#10;
    MsgBox(FmtMessage(CustomMessage('DicFailed'), [Msg, Log]), mbError, MB_OK);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if CurStep = ssInstall then
  begin
    { stop a running server so that its files can be replaced }
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/f /im naggy-server.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  end;
  if CurStep = ssPostInstall then
  begin
    PrepareDictionaries();
    { add naggy_win to the keyboard list of the current user }
    ExecAsOriginalUser(ExpandConstant('{app}\naggy-dictool.exe'), 'enable-tip', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code);
    { start the server for the current user }
    ExecAsOriginalUser(ExpandConstant('{app}\naggy-server.exe'), '', ExpandConstant('{app}'), SW_HIDE, ewNoWait, Code);
  end;
end;
