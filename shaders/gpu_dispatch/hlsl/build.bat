:: Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
::
:: This is a simple script to compile all required permutations of app shaders.
:: Requires Vulkan SDK (VULKAN_SDK environment variable set) with a suitably recent dxc build.

@echo off
setlocal
set DXC=%VULKAN_SDK%\Bin\dxc.exe

if not exist %DXC% (
    echo Please make sure the latest Vulkan SDK is installed to provide a dxc executable.
    exit /b 1
) else (
    echo Using %DXC%
)

:: Output and input files: -Fo shader.spv shader.hlsl
:: Must disable validation and legalization (-Vd -fcgl) of SPIR-V to handle the shaders with extension opcodes.
set COMPILE=%DXC% -spirv -fspv-target-env=vulkan1.3 -enable-16bit-types
set COMPILE_GWG=%DXC% -spirv -fspv-target-env=vulkan1.3 -enable-16bit-types -Vd -fcgl

:: Cleanup; Ensure we're in the correct directory
cd %~dp0
rmdir /q/s spv
mkdir spv

:: Process all shaders
call :compile_file vs geometry_vs
call :compile_file ps geometry_ps
call :compile_file vs geometry_material_map_vs
call :compile_file ps geometry_material_map_ps

call :compile_file_gwg cs sanity_aggregation_cs
call :compile_file_gwg cs sanity_dynamic_exp_cs
call :compile_file_gwg cs sanity_entry_cs
call :compile_file_gwg cs sanity_fixed_exp_cs

:: These shaders require preprocessor variants
call :compile_file_gwg_variants cs classify_gpu_enqueue_cs
call :compile_file_gwg_variants cs classify_material_map_gpu_enqueue_cs
call :compile_file_gwg_variants cs compose_gpu_enqueue_cs
call :compile_file_gwg_variants cs compose_material_map_gpu_enqueue_cs

:: Done
exit /b 0


:: Functions
:compile_file
echo %~2.hlsl
%COMPILE% -T %~1_6_7 -Fo spv\%~2.spv %~2.hlsl
goto :eof

:compile_file_gwg
echo %~2.hlsl
%COMPILE_GWG% -T %~1_6_7 -Fo spv\%~2.spv %~2.hlsl
goto :eof

:compile_file_gwg_variants
echo %~2.hlsl
%COMPILE_GWG% -T %~1_6_7                           -Fo spv\%~2_fe.spv %~2.hlsl
%COMPILE_GWG% -T %~1_6_7 -D NODE_DYNAMIC_EXPANSION -Fo spv\%~2_de.spv %~2.hlsl
%COMPILE_GWG% -T %~1_6_7 -D NODE_AGGREGATION       -Fo spv\%~2_a.spv  %~2.hlsl
goto :eof
