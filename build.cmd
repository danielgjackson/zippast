@ECHO OFF
SETLOCAL EnableDelayedExpansion
CD /D %~dp0

rem If launched from anything other than cmd.exe, will have "%WINDIR%\system32\cmd.exe" in the command line
set INTERACTIVE_BUILD=
IF "%1"=="/NONINTERACTIVE" GOTO NONINTERACTIVE
ECHO.%CMDCMDLINE% | FINDSTR /C:"%COMSPEC% /c" >NUL
IF ERRORLEVEL 1 GOTO NONINTERACTIVE
rem Preserve this as it seems to be corrupted below otherwise?!
SET CMDLINE=%CMDCMDLINE%
rem If launched from anything other than cmd.exe, last character of command line will always be a double quote
IF NOT ^!CMDCMDLINE:~-1!==^" GOTO NONINTERACTIVE
rem If run from Explorer, last-but-one character of command line will be a space
IF NOT "!CMDLINE:~-2,1!"==" " GOTO NONINTERACTIVE
SET INTERACTIVE_BUILD=1
:NONINTERACTIVE

SET FIND_CL=
FOR %%p IN (cl.exe) DO SET "FIND_CL=%%~$PATH:p"
IF DEFINED FIND_CL (
  ECHO Build tools already on path.
  GOTO BUILD
)
ECHO Build tools not on path, searching for them...
CALL vc.bat
IF ERRORLEVEL 1 GOTO ERROR

:BUILD
SET NOLOGO=/nologo
ECHO Compiling...
cl %NOLOGO% -c /EHsc /DUNICODE /D_UNICODE /Tc"zippast.c"
IF ERRORLEVEL 1 GOTO ERROR
ECHO Linking...
link %NOLOGO% /out:zippast.exe zippast /subsystem:console
IF ERRORLEVEL 1 GOTO ERROR
ECHO Done

IF DEFINED INTERACTIVE_BUILD COLOR 2F & PAUSE & COLOR
GOTO :EOF

:ERROR
ECHO ERROR: An error occured.
IF DEFINED INTERACTIVE_BUILD COLOR 4F & PAUSE & COLOR
EXIT /B 1
GOTO :EOF
