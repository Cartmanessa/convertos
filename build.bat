@echo off
fasm bootsect.asm bootsect.bin
if errorlevel 1 goto :fail
cl.exe /GS- /c kernel.cpp
if errorlevel 1 goto :fail
link.exe /OUT:kernel.bin /BASE:0x10000 /FIXED /FILEALIGN:512 /MERGE:.rdata=.data /IGNORE:4254 /NODEFAULTLIB /ENTRY:kmain /SUBSYSTEM:NATIVE kernel.obj
if errorlevel 1 goto :fail
echo Build OK
exit /b 0
:fail
echo Build failed
exit /b 1
