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
call :compile_file vs  geometry_vs
call :compile_file ps  geometry_ps
call :compile_file vs  geometry_material_map_vs
call :compile_file ps  geometry_material_map_ps
call :compile_file lib sanity_aggregation_cs
call :compile_file lib sanity_dynamic_exp_cs
call :compile_file lib sanity_entry_cs
call :compile_file lib sanity_fixed_exp_cs

:: These shaders require preprocessor variants
call :compile_file_variants lib classify_gpu_enqueue_cs
call :compile_file_variants lib classify_material_map_gpu_enqueue_cs
call :compile_file_variants lib compose_gpu_enqueue_cs
call :compile_file_thread   lib compose_gpu_enqueue_cs
call :compile_file_variants lib compose_material_map_gpu_enqueue_cs

:: Done
exit /b 0

:: Functions
:compile_file
echo %~2.hlsl
%COMPILE% -T %~1_6_8 -Fo spv\%~2.spv %~2.hlsl
goto :eof

:compile_file_variants
echo %~2.hlsl
%COMPILE% -T %~1_6_8                           -Fo spv\%~2_fe.spv %~2.hlsl
%COMPILE% -T %~1_6_8 -D NODE_DYNAMIC_EXPANSION -Fo spv\%~2_de.spv %~2.hlsl
%COMPILE% -T %~1_6_8 -D NODE_AGGREGATION       -Fo spv\%~2_a.spv  %~2.hlsl
goto :eof

:compile_file_thread
echo %~2.hlsl (thread)
%COMPILE% -T %~1_6_8 -D NODE_THREAD            -Fo spv\%~2_t.spv  %~2.hlsl
goto :eof
