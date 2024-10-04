:: Copyright (c) 2023-2024 Advanced Micro Devices, Inc. All Rights Reserved.
::
:: This is a simple script to compile all required permutations of app shaders.
:: Requires a dxc compiler build with GPU Work Graphs support and SPIR-V code generation.

@echo off
setlocal

:: Check if dxc is available
if not [%1]==[] (
    echo Using dxc binary %1
    set DXC=%1
) else (
    echo Using dxc binary from PATH:
    set DXC=dxc
)

%DXC% --version 2>nul

if errorlevel 1 (
    echo dxc not found. Provide the path to dxc as the first argument.
    exit /b 1
)

echo.
echo NOTE: dxc build must support GPU Work Graphs and SPIR-V code generation.
echo.

:: Output and input files: -Fo shader.spv shader.hlsl
set COMPILE=%DXC% -spirv -fspv-target-env=vulkan1.3 -enable-16bit-types

:: Cleanup; Ensure we're in the correct directory
cd %~dp0
rmdir /q/s spv
mkdir spv

:: Process all shaders
call :compile_file ps  geometry_forward_ps
call :compile_file lib compute_to_mesh_multi_cs
call :compile_file lib geometry_mesh_ms
call :compile_file lib geometry_mesh_multi_ms
call :compile_file lib compute_to_mesh_cs
call :compile_file_share_input lib geometry_mesh_multi_ms
call :compile_file_share_input lib compute_to_mesh_multi_cs

:: Done
exit /b 0

:: Functions
:compile_file
echo %~2.hlsl
%COMPILE% -T %~1_6_9 -Fo spv\%~2.spv %~2.hlsl
goto :eof

:compile_file_share_input
echo %~2.hlsl (USE_INPUT_SHARING)
%COMPILE% -T %~1_6_9 -D USE_INPUT_SHARING -Fo spv\%~2_share_input.spv %~2.hlsl
goto :eof
