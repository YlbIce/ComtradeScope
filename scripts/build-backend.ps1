param(
  [string]$Configuration = "Release",
  [string]$Generator = "Ninja",
  [string]$VcpkgRoot = ""
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Backend = Join-Path $Root "backend"
$Build = Join-Path $Backend "build"
$Bin = Join-Path $Backend "bin"

$env:VSLANG = "1033"

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
  $candidate = Resolve-Path (Join-Path $Root "..\tools\vcpkg") -ErrorAction SilentlyContinue
  if ($candidate) {
    $VcpkgRoot = $candidate.Path
  }
}

function Import-VsDevEnvironment {
  $candidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
  )
  $vsDevCmd = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $vsDevCmd) {
    throw "未找到 VS 2022 VsDevCmd.bat，无法配置 MSVC 编译环境。"
  }

  $environmentLines = cmd /s /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 > nul && set"
  foreach ($line in $environmentLines) {
    $index = $line.IndexOf("=")
    if ($index -le 0) {
      continue
    }
    $name = $line.Substring(0, $index)
    $value = $line.Substring($index + 1)
    if ($name.Equals("Path", [StringComparison]::OrdinalIgnoreCase)) {
      $name = "PATH"
    }
    [Environment]::SetEnvironmentVariable($name, $value, "Process")
  }
}

Import-VsDevEnvironment
$env:VSLANG = "1033"

$vsNinja = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if ($Generator -eq "Ninja" -and -not (Get-Command ninja.exe -ErrorAction SilentlyContinue) -and (Test-Path $vsNinja)) {
  $env:PATH = "$(Split-Path $vsNinja);$env:PATH"
}

if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or -not (Test-Path (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
  throw "未找到 vcpkg。请传入 -VcpkgRoot，或放在工作区 ..\tools\vcpkg。"
}

$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$configureArgs = @(
  "-S", $Backend,
  "-B", $Build,
  "-G", $Generator,
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
  "-DVCPKG_TARGET_TRIPLET=x64-windows",
  "-DCMAKE_BUILD_TYPE=$Configuration"
)

cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
  throw "CMake 配置失败。"
}
cmake --build $Build --config $Configuration
if ($LASTEXITCODE -ne 0) {
  throw "CMake 构建失败。"
}

New-Item -ItemType Directory -Force -Path $Bin | Out-Null
$ExeName = if ($IsWindows -or $env:OS -eq "Windows_NT") { "comtradescope-backend.exe" } else { "comtradescope-backend" }
$Candidates = @(
  (Join-Path $Build "$Configuration\$ExeName"),
  (Join-Path $Build $ExeName)
)
$Exe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Exe) {
  throw "未找到后端可执行文件：$ExeName"
}

Copy-Item $Exe (Join-Path $Bin $ExeName) -Force

$exeDir = Split-Path $Exe
Get-ChildItem -Path $exeDir -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $Bin $_.Name) -Force
}

Write-Host "后端构建完成：$(Join-Path $Bin $ExeName)"
