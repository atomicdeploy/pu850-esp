@echo off
cls

cd /d %~dp0
setlocal enableextensions
chcp 65001 >nul

title Build PU850

for /F "tokens=1-4 delims=:.," %%a in ("%time%") do set startTime=%%a:%%b:%%c.%%d

:: Set environment variables
set VSCA_WORKSPACE_DIR=%~dp0
set VSCA_BUILD_DIR=%VSCA_WORKSPACE_DIR%\Build
set VSCA_SKETCH=ASA0002E.ino

set log_file=%VSCA_WORKSPACE_DIR%\build.log

set build.cmd=%~nx0

set arduino_cli_flags=--config-file %VSCA_WORKSPACE_DIR%\.vscode\arduino-cli.yaml

set build_flags=-DARDUINO_CLI -DUSE_LOCALH

set ARDUINO_UPDATER_ENABLE_NOTIFICATION=false

:: Check if the user has set any custom flags in the environment variables
if "%DEBUG_TOOLS%" == "1" set build_flags=%build_flags% -DDebugTools
if "%DEBUG_ON_SERIAL%" == "1" set build_flags=%build_flags% -DDebugOnSerial
if "%SHELL_ON_SERIAL%" == "1" set build_flags=%build_flags% -DShellOnSerial
if "%SKIP_LIBRARIES_DISCOVERY%" == "1" set arduino_cli_flags=%arduino_cli_flags% --skip-libraries-discovery

:: Parse command line arguments
call :parseArgs %*

if errorlevel 1 exit /b 1

:: List of commands to check
set commands=powershell hostname whoami arduino-cli gzip md5sum touch sed ls

:: Loop through each command and check if it's in the PATH
for %%c in (%commands%) do (
	call :ensureCommandExists %%c
	if errorlevel 1 (
		exit /b 1
	)
)

:: Check Arduino CLI version
call :ensureArduinoCliVersion
if errorlevel 1 (
	exit /b 1
)

call :displayInfo "Building %VSCA_SKETCH%"

:: Check if Build directory exists, if not, create it
if not exist "%VSCA_BUILD_DIR%" (
	mkdir "%VSCA_BUILD_DIR%"
	if errorlevel 1 (
		call :displayError "Failed to create Build directory!"
		exit /b 1
	)
)

:: Pre-build steps
call "%VSCA_WORKSPACE_DIR%\.vscode\prebuild.cmd"
if errorlevel 1 (
	call :displayError "Prebuild script failed!"
	exit /b 1
)

:: Clean old build files
del "%VSCA_BUILD_DIR%\%VSCA_SKETCH%.*" /f /q /a >nul 2>&1
if errorlevel 1 (
	call :displayWarning "Failed to clean old build files!"
)

:: Remove build.log
if exist "%log_file%" (
	del "%log_file%" /f /q /a >nul 2>&1
	if errorlevel 1 (
		call :displayWarning "Failed to delete %log_file%!"
	)
)

:: Set the total flash size of the ESP8266 module in MB
set flashSize=1

set board_params=xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=basic,mmu=3232,non32xfer=fast,ResetMethod=nodemcu,CrystalFreq=26,FlashFreq=40,FlashMode=qio,eesz=%flashSize%M,led=2,sdk=nonosdk_190703,ip=lm2f,dbg=Disabled,lvl=None____,wipe=none,baud=115200

:: Compile the sketch
:: Note: --build-cache-path was removed as it's deprecated in newer Arduino CLI versions
set build_command=arduino-cli compile --verbose --fqbn esp8266:esp8266:generic:%board_params% --export-binaries --build-property compiler.cache_core=false --build-property mkbuildoptglobals.extra_flags="--no_cache_core" --build-property "--build.opt.flags=" --build-property build.extra_flags="-Wall %build_flags%" --build-path "%VSCA_BUILD_DIR%" --jobs 0 --log-level trace --log-file "%log_file%" %arduino_cli_flags%

powershell -Command "$nl = $('' | Out-String); . { Invoke-Expression ($Env:build_command) } *>&1 | ForEach-Object { if ($_ -is [System.Management.Automation.ErrorRecord]) { ('[93m' + $($_.Exception.Message -replace '^(\..+)$', ('[92;1m$1[0m') -replace '^([║╠╚═]+)(.*)$', ('[0;96m$1[0m$2[0m') -replace '^(Error.+|.+fatal error.+)$', ('[91;1m$1[0m')) + '[0m') } else { $_.Replace('\\', '\').Replace('%VSCA_BUILD_DIR%', 'Build').Replace('%VSCA_WORKSPACE_DIR%', '').Replace('%USERPROFILE%', '~') -replace 'sketch', 'project' -replace 'esp8266[\\\/]hardware[\\\/]esp8266[\\\/](\d\.)+[\\\/]\w+[\\\/]', '' -replace '~[\\\/](Documents[\\\/]Arduino|AppData[\\\/]Local[\\\/]Arduino\d+[\\\/]packages[\\\/]esp8266[\\\/]hardware[\\\/]esp8266[\\\/][\d\.]*)[\\\/](libraries|bootloaders)[\\\/]', '' -replace '^(""?~\\AppData\\Local\\Arduino\d+\\(packages|internal)\\.+|default_encoding:.+|Alternatives for.+|Preferences override,.+|To change,.+|\s+Read more at.+|Tip:.+|Using (cached|previously|precompiled|library|board|core|global include).+)$', '[A' -replace '^(FQBN:)(.+)$', '[92;1m$1[0m$2[A' -replace '^(\s+)->', '$1→' -replace '^(.+)\.{3}$', ($nl + '[92;1m>> [93;1m$1[0m') -replace '^(.*[Ll]ibrary.+)$', ('[90m$1[0m') } }"

pushd "%VSCA_BUILD_DIR%"

if not exist "%VSCA_SKETCH%.bin" (
	call :displayError "Compilation failed!"
	exit /b 1
)

:: Get the size of the binary file
for /f "tokens=5" %%a in ('ls -l "%VSCA_BUILD_DIR%\%VSCA_SKETCH%.bin"') do set size=%%a
set /a totalUsage=100*%size%/(%flashSize%*1048576)

:: Display the program size and flash usage
echo.
echo.[92;1mProgram Size:[0m [0m%size%[0m bytes ([95;1m%totalUsage%%%[0m of flash used)

:: Calculate the MD5 checksum of the raw binary file (uncompressed)
md5sum "%VSCA_SKETCH%.bin" > "%VSCA_SKETCH%.bin.md5.tmp"
if errorlevel 1 (
	call :displayError "Failed to generate MD5 checksum!"
	exit /b 1
)

:: Compress the binary file with gzip
call :displayInfo "Compressing binary file"
gzip -f -9 "%VSCA_SKETCH%.bin"
if errorlevel 1 (
	call :displayError "Compression failed!"
	exit /b 1
)

:: Rename the compressed file back to .bin
ren "%VSCA_SKETCH%.bin.gz" "%VSCA_SKETCH%.bin"
if errorlevel 1 (
	call :displayError "Failed to rename compressed file!"
	exit /b 1
)

:: Recalculate the MD5 checksum of the compressed file
md5sum "%VSCA_SKETCH%.bin" | sed -e "s/%VSCA_SKETCH%.bin/\0 (compressed)/g" >> "%VSCA_SKETCH%.bin.md5.tmp"
if errorlevel 1 (
	call :displayError "Failed to generate MD5 checksum after compression!"
	exit /b 1
)

:: Update the timestamp of the binary file
touch "%VSCA_SKETCH%.bin"
if errorlevel 1 (
	call :displayError "Failed to update the file timestamp!"
	exit /b 1
)

:: Get the size of the compressed binary file
for /f "tokens=5" %%a in ('ls -l "%VSCA_BUILD_DIR%\%VSCA_SKETCH%.bin"') do set compressedSize=%%a
set /a compressionRatio=(%compressedSize%*100/%size%)

:: Display file sizes
echo.
ls -lh "%VSCA_SKETCH%.bin" -GNp --color --time-style=long-iso
if errorlevel 1 (
	call :displayError "Failed to list file sizes!"
	exit /b 1
)

echo Compressed to [92;1m%compressionRatio%%%[0m of original size

:: Clean up the local header file
del "%VSCA_WORKSPACE_DIR%\~local.h" /f /q /a >nul 2>&1
if errorlevel 1 (
	call :displayError "Failed to delete ~local.h!"
	exit /b 1
)

call :displaySuccess "Build completed successfully!"

:: Display the MD5 checksum of the binary file
echo.
type "%VSCA_SKETCH%.bin.md5.tmp" | sed -E ^
	-e "s/([a-f0-9]{32})/\x1b[32m\1\x1b[0m/" ^
	-e "s/(\*\S+)/\x1b[33m\1\x1b[0m/" ^
	-e "s/(\(compressed\))/\x1b[34m\1\x1b[0m/"

:: Save the final MD5 checksum file
move /y "%VSCA_SKETCH%.bin.md5.tmp" "%VSCA_SKETCH%.bin.md5" >nul 2>&1
if errorlevel 1 (
	call :displayError "Failed to save the MD5 checksum file!"
	exit /b 1
)

popd

for /F "tokens=1-4 delims=:.," %%a in ("%time%") do set endTime=%%a:%%b:%%c.%%d

:: Display the total sizes and compression ratio
:: echo.
:: echo Program size: [96;1m%size%[0m bytes ([95;1m%totalUsage%%%[0m of flash used)
:: echo Compressed: [93;1m%compressedSize%[0m bytes ([92;1m%compressionRatio%%%[0m of original size)

:: Display the elapsed time
echo.
call :elapsedTime %startTime% %endTime%

exit /b 0

:ensureCommandExists
	:: Check if a command exists in the system
	where %1 >nul 2>nul
	if errorlevel 1 (
		call :displayError "Command '%1' not found! Make sure it's installed and in the PATH."
		exit /b 1
	)
exit /b 0

:parseVersion
    for /f "tokens=1,2,3 delims=." %%a in ("%1") do (
        set major=%%a
        set minor=%%b
        set patch=%%c
    )
exit /b

:compareVersions
	call :parseVersion %1
	set major1=%major%
	set minor1=%minor%
	set patch1=%patch%

	call :parseVersion %2
	set major2=%major%
	set minor2=%minor%
	set patch2=%patch%

	if %major1% gtr %major2% exit /b 1
	if %major1% lss %major2% exit /b -1
	if %minor1% gtr %minor2% exit /b 1
	if %minor1% lss %minor2% exit /b -1
	if %patch1% gtr %patch2% exit /b 1
	if %patch1% lss %patch2% exit /b -1
exit /b 0

:ensureArduinoCliVersion
	set minimum_version=1.0.0

	set "found_version="

	for /f "tokens=1,3 usebackq" %%a in (`arduino-cli version 2^>nul`) do (
		if /i "%%a"=="arduino-cli" (
			set found_version=%%b
		) else (
			call :displayError "'arduino-cli version' returned an improper version string!"
			exit /b 1
		)
	)

	call :compareVersions %minimum_version% %found_version%

	if %errorlevel% equ 1 (
		call :displayError "The installed arduino-cli version is %found_version%, but at least %minimum_version% is required."
		exit /b 1
	)
exit /b 0

:elapsedTime
	set options="tokens=1-4 delims=:.,"
	for /f %options% %%a in ("%~1") do set s_h=%%a&set /a s_m=100%%b %% 100&set /a s_s=100%%c %% 100&set /a s_ms=100%%d %% 100
	for /f %options% %%a in ("%~2") do set e_h=%%a&set /a e_m=100%%b %% 100&set /a e_s=100%%c %% 100&set /a e_ms=100%%d %% 100

	set /a h=e_h-s_h, m=e_m-s_m, s=e_s-s_s, ms=e_ms-s_ms
	if %ms% lss 0 set /a s-=1, ms+=100
	if %s% lss 0 set /a m-=1, s+=60
	if %m% lss 0 set /a h-=1, m+=60
	if %h% lss 0 set /a h+=24

	set /a totalsecs=h*3600 + m*60 + s
	if 1%ms% lss 100 set ms=0%ms%
	echo Elapsed Time: [93;1m%totalsecs%.%ms%s[0m total
exit /b 0

:displayError
	:: Display an error message in red, with three beeps
	echo.
	rundll32 user32.dll,MessageBeep
	echo [91;1m[ERROR][0m %~1
exit /b 0

:displayWarning
	:: Display a warning message in yellow
	echo.
	echo [93;1m[WARNING][0m %~1
exit /b 0

:displayInfo
	:: Display an information message in cyan
	echo.
	echo [96;1m[INFO][0m %~1
exit /b 0

:displaySuccess
	:: Display a success message in green
	echo.
	echo [92;1m[SUCCESS][0m %~1
exit /b 0

:parseArgs
	if "%~1" == "" (
		goto parseArgsEnd
	)

	if "%~1" == "--help" (
		echo.
		echo [93;1mUsage: %build.cmd% [options][0m
		echo.
		echo [93;1mOptions:[0m
		echo [92;1m  --help[0m                Display this help message
		echo.
		echo [92;1m  --debug-tools[0m         Enable ASA debug tools for dumping variables using web server
		echo [92;1m  --debug-on-serial[0m     Prints ESP debug messages on serial
		echo [92;1m  --shell-on-serial[0m     Enable shell on serial instead of PU850
		echo.
		echo [92;1m  --dump-profile[0m        Dump the Arduino build profile
		echo [92;1m  --profile [profile][0m   Use the specified Arduino build profile
		echo.
		exit /b 1
	) else if "%~1" == "--debug-tools" (
		:: call :displayWarning "Enabling debug tools"
		set build_flags=%build_flags% -DDebugTools
	) else if "%~1" == "--debug-on-serial" (
		:: call :displayWarning "Enabling debug on serial"
		set build_flags=%build_flags% -DDebugOnSerial
	) else if "%~1" == "--shell-on-serial" (
		:: call :displayWarning "Enabling shell on serial"
		set build_flags=%build_flags% -DShellOnSerial
	) else if "%~1" == "--dump-profile" (
		call :displayInfo "Dumping the Arduino build profile"
		set arduino_cli_flags=%arduino_cli_flags% --dump-profile
	) else if "%~1" == "--skip-libraries-discovery" (
		call :displayInfo "Skipping discovery of used libraries"
		set arduino_cli_flags=%arduino_cli_flags% --skip-libraries-discovery
	) else if "%~1" == "--profile" (
		if "%2" == "" (
			call :displayError "Missing profile name!"
			exit /b 1
		)
		call :displayInfo "Using build profile: %2"
		set arduino_cli_flags=%arduino_cli_flags% --profile %2
		shift
	) else (
		call :displayWarning "Unknown argument: %~1"
	)

	shift
	goto parseArgs

	:parseArgsEnd
exit /b 0
