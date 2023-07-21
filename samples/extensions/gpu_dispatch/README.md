Hotkeys
-------

Q, W, E - in work graph mode, highlight the classified shaders

Command line options
--------------------

You can select various options (some can be combined) when running the application:

    vulkan_samples [--log-fps] [--width W] [--height H] [-o <option> [<option> ...]] sample gpu_dispatch

NOTE: Not all combinations of options are compatible, and some may significantly reduce the performance.

Available options:

    hlsl                    instead of using GLSL shaders (compiled on-line), use the precompiled
                            HLSL shaders (they can be recompiled with a batch script)

    scene_teapot            the default scene with a teapot model
    scene_monkeys           a scene with multiple Blender monkey heads
    scene_material_1        a static scene based on a captured material id map
    scene_material_2        same as above, but scene 2
    scene_sanity            forces work graphs mode, uses a graph with four nodes and very basic
                            shader that assigns a color to a tile in the screen:
                            red - fixed exp., green - dynamic exp, white - aggregation.

    no_animation            show a still frame
    present_single          will insert 2 second sleep between each frame
    present_burst           will dispay three frames, then sleep for 2 seconds
    clear_image             will clear the swapchain image with blue color before drawing

    alu_complexity_X        change the arithmetic complexity of the scene, 0 to 100,
                            where 0 = 0.0 and 100 = 1.0 (maximum).

    reset_scratch           initialize the graph backing store/scratch space on every frame


The following options are not supported in the "sanity" scene:

    materials_1             sets the number of materials used by the model.
                            1 material is 3 shaders (background, model, edge)
    materials_2             2 materials is 7 shaders
    materials_3             3 materials is 15 shaders
    materials_4             4 materials is 31 shaders
    materials_5             5 materials is 63 shaders
    materials_6             6 materials is 127 shaders
    materials_7             7 materials is 255 shaders
    materials_8             8 materials is 511 shaders
    materials_9             9 materials is 1023 shaders

    graph_fixed_exp         fixed expansion nodes tile classification
    graph_dynamic_exp       just to test the dynamic code path, same logic as fixed expansion
    graph_aggregation       per-pixel classification with aggregation node


The following options apply to "monkeys" scene:

    textures_X              number of textures per material, 0 to 16
    instances_X             number of instances, 1 to 1024
