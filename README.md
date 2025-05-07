# Trisualizer

Trisualizer is an advanced two-variable function grapher that allows users to visualize and interact with multivariable mathematical functions in real-time.

It features capabilities for displaying the partial derivatives, the gradient vector, the normal vector, and the tangent plane at any selected point on the surface, computing double and surface integrals over general regions and line integrals of parametric curves using numerical methods.

![ui](https://github.com/user-attachments/assets/6ed5c52a-f31e-4f20-a0f4-bfa13ceffeba)

Users can define custom variables, values of which can be adjusted with sliders. The grapher features realistic shading using the Blinn-Phong lighting model alongside 5 different coloring methods. Grid lines can also be displayed.

The project is written in C++ and uses OpenGL. Compute shaders running the GPU are utilized for computations.

## Building

### Dependencies

- Git
- CMake version >= 3.21
- C++ build system (Make, Ninja, MSBuild, etc.)
- C++ compiler (GCC, Clang, MSVC, etc.)

Clone the repository with `--recurse-submodules`, then go into the directory
```
git clone --recurse-submodules https://github.com/Yilmaz4/Trisualizer.git
cd Trisualizer
```

Generate the build files with CMake and build
```
cmake -S . -B build
cmake --build build
```

You can then find the binary in the `bin` directory

The project has been tested on Windows and Linux.

## To-do

- Add support for implicit functions using marching cubes algorithm and parametric surfaces
- Visualize curl and divergence
- Visualize gradient vector field
- Let user select preset views
- Let user save points on the surface of a specific function (points of interest)
- Show local minima, maxima and saddle points
- Let user slice the graph and see the cross section
- Add soft shadows under vector arrows
- Let user export high resolution images
- Fix graph getting slightly smaller when grid resolution is lowered
- Implement cross-platform file dialogs in functions `save_file` and `open_file`
