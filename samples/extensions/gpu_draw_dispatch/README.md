Command line options
--------------------

You can select various options (some can be combined) when running the application:

    vulkan_samples [--log-fps] [--width W] [--height H] [-o <option> [<option> ...]] sample gpu_draw_dispatch

NOTE: Not all combinations of options are compatible. Some may be not implemented or unsupported by the API.

Available options:

    no_animation                    show a still frame
    draw_node (or no options)       dispatch a work graph with a mesh draw node
    compute_draw_node               dispatch a work graph with a compute node that will issue a mesh draw
    draw_node multi_all             a work graph with many nodes
    compute_draw_node multi_all     .. but a compute node launches mesh draws

A few more specialized examples:

    draw_node node_info             an example of overriding node name in the API
    share_input                     mesh draw node shares input (the same payload is sent to multiple nodes)
