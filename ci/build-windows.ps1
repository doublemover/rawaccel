$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (!$vs) { throw 'Visual Studio with MSBuild was not found.' }
# The WDK NuGet package ships x64 host tools, including ApiValidator.
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\amd64\MSBuild.exe'
& nuget restore ci\packages.config -PackagesDirectory packages -NonInteractive
if ($LASTEXITCODE) { throw 'WDK/SDK restore failed.' }
& nuget restore rawaccel.sln -NonInteractive
if ($LASTEXITCODE) { throw 'Solution package restore failed.' }
# Compile only. Never install a driver, create a test certificate, or change boot/security settings.
& $msbuild rawaccel.sln /m /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:EnableInf2cat=false /bl:windows-build.binlog
if ($LASTEXITCODE) { throw 'Windows solution build failed.' }
$testRunner = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
& $testRunner wrapper-tests\bin\Release\wrapper-tests.dll /Platform:x64 /Logger:trx
if ($LASTEXITCODE) { throw 'Managed regression tests failed.' }
$developerShell = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$nativeCommand = 'call "{0}" -arch=x64 && cl /nologo /std:c++17 /EHsc /O2 /W4 /Icommon tests\native\math_tests.cpp /Fe:math_tests.exe && math_tests.exe && cl /nologo /std:c++17 /EHsc /O2 /fp:fast /W4 /Icommon tests\native\input_tests.cpp /Fe:input_tests.exe && input_tests.exe' -f $developerShell
& cmd /d /s /c $nativeCommand
if ($LASTEXITCODE) { throw 'MSVC native regression tests failed.' }
