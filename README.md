
# Elec3D
Elec3D is an interactive 3D circuit-layout visualizer built with C++17, OpenGL, GLFW, GLAD, GLM, ImGui, Eigen, and nlohmann/json. It loads a JSON circuit layout, renders components and wires in a 3D scene, and lets the user inspect, edit, connect, save, and reload the circuit through an ImGui interface.

## Quick Start on Windows

Use these commands if you are on Windows with Visual Studio C++ tools installed.

1. Open PowerShell.
2. Go to the project folder:

```powershell
cd "C:\path\to\Elec3D"
```

3. Configure CMake:

```powershell
cmake -S . -B build
```

4. Build the Debug executable:

```powershell
cmake --build build --config Debug
```

5. Run the app from inside the `build` folder:

```powershell
cd build
.\bin\Debug\Elec3D.exe
```

If everything is set up correctly, a window titled `Welcome to Elec3D` should open.

## Very Important Runtime Rule

Run the executable with `build` as the current working directory.

The program loads files using these relative paths:

```text
../src/layout.json
../shaders/cube.vert
../shaders/cube.frag
```

That means this is correct:

```powershell
cd "C:\path\to\Elec3D\build"
.\bin\Debug\Elec3D.exe
```

This is not recommended:

```powershell
cd "C:\path\to\Elec3D"
.\build\bin\Debug\Elec3D.exe
```

If you run from the project root, the app may fail with:

```text
Failed to open layout.json
```

or:

```text
Failed to load shader files.
```

## Requirements

Install these before building:

- CMake 3.10 or newer
- A C++17-capable compiler
- OpenGL-capable graphics drivers
- Windows option: Visual Studio 2019 or Visual Studio 2022 with `Desktop development with C++`
- Linux option: GCC or Clang plus OpenGL development packages
- macOS option: Xcode command line tools

Bundled dependencies already included in this repository:

- GLFW
- GLAD
- GLM
- ImGui
- nlohmann/json
- Eigen

Because these libraries are included in the repository, you normally do not need to install them separately.

## Check Your Tools

Before building, you can confirm CMake is available:

```powershell
cmake --version
```

On Windows, if `cmake -S . -B build` cannot find a compiler, install Visual Studio with the C++ desktop workload, then open a new PowerShell window and try again.

## Project Structure

```text
Elec3D/
|-- CMakeLists.txt              # Main CMake build file
|-- README.md                   # Project documentation
|-- src/
|   |-- main.cpp                # Main application source
|   `-- layout.json             # Default layout loaded at startup
|-- shaders/
|   |-- cube.vert               # Vertex shader
|   `-- cube.frag               # Fragment shader
|-- libs/
|   |-- glad/                   # OpenGL function loader
|   |-- glfw/                   # Window and input library
|   |-- glm/                    # OpenGL math library
|   |-- imgui/                  # UI library
|   `-- json/                   # nlohmann/json
|-- include/
|   `-- eigen3/                 # Eigen headers
`-- build/                      # Generated build files and executables
```

## Build and Run on Windows

### Debug Build

From the project root:

```powershell
cmake -S . -B build
cmake --build build --config Debug
cd build
.\bin\Debug\Elec3D.exe
```

### Release Build

From the project root:

```powershell
cmake -S . -B build
cmake --build build --config Release
cd build
.\bin\Release\Elec3D.exe
```

### Running Again Later

After the project has already been built, you do not need to configure again unless `CMakeLists.txt` or dependencies changed.

To run Debug again:

```powershell
cd "C:\path\to\Elec3D\build"
.\bin\Debug\Elec3D.exe
```

To rebuild and run Debug again:

```powershell
cd "C:\path\to\Elec3D"
cmake --build build --config Debug
cd build
.\bin\Debug\Elec3D.exe
```

## Build and Run with Visual Studio

1. Open PowerShell in the project root.
2. Generate the Visual Studio solution:

```powershell
cmake -S . -B build
```

3. Open this file in Visual Studio:

```text
build/Elec3D.sln
```

4. In Solution Explorer, right-click `Elec3D` and choose `Set as Startup Project`.
5. Select `Debug` or `Release` from the Visual Studio toolbar.
6. Press `Ctrl+F5` to run without debugging, or `F5` to run with debugging.

If Visual Studio starts the app but the app cannot find `layout.json` or the shaders, set the debugging working directory to the `build` folder:

```text
C:\path\to\Elec3D\build
```

## Build and Run on Linux

From the project root:

```bash
cmake -S . -B build
cmake --build build
cd build
./bin/Elec3D
```

If CMake cannot find OpenGL or X11 development files on Ubuntu/Debian, install:

```bash
sudo apt install build-essential cmake libgl1-mesa-dev xorg-dev
```

Then run the CMake commands again.

## Build and Run on macOS

Install Xcode command line tools:

```bash
xcode-select --install
```

Then build and run from the project root:

```bash
cmake -S . -B build
cmake --build build
cd build
./bin/Elec3D
```

## What the App Loads at Startup

At launch, Elec3D reads:

```text
src/layout.json
```

The file must contain:

- `components`: the list of circuit components
- `connections`: the list of wires between component IDs

Example component:

```json
{
  "id": 0,
  "type": "Battery",
  "position": [0.0, 0.0, 0.0],
  "layer": 1
}
```

Example connection:

```json
{
  "from": 0,
  "to": 1
}
```

Supported component types:

- `Battery`
- `Resistor`
- `Capacitor`
- `Inductor`
- `Diode`

Default values assigned in code:

- Battery: 0.1 ohm internal resistance and 5.0 V
- Resistor: 1.0 ohm
- Capacitor: 0.5 ohm
- Inductor: 0.8 ohm
- Diode: 2.0 ohms

## Controls

Camera controls:

- Left mouse drag: orbit around the circuit
- Mouse wheel: zoom in and out

Layer controls:

- Number keys `0` through `9`: toggle that layer on or off
- `Layer Control` panel: toggle visible layers with checkboxes

Main UI panels:

- `Display Options`: show or hide the grid
- `Voltage Watcher`: select a component and see its voltage history graph
- `Simulation Settings`: choose the ground node
- `Add Component`: create a new component by type, layer, and position
- `Connect Components`: connect two component IDs
- `Toggle Signal Per Connection`: enable or disable animated signal flow
- `Edit Component`: edit the selected component's type, resistance, voltage, position, and layer

## Saving and Loading Layouts

The app starts from:

```text
src/layout.json
```

When you click `Save Layout to Json`, the app writes:

```text
build/output_layout.json
```

This file is created in `build` because the app should be run from the `build` directory.

When you click `Load Layout`, the app reads:

```text
build/output_layout.json
```

The save/load workflow is:

1. Start the app.
2. Add or edit components.
3. Add or edit connections.
4. Click `Save Layout to Json`.
5. Close and reopen the app if needed.
6. Click `Load Layout` to restore the saved runtime layout.

Saving does not overwrite `src/layout.json`.

## Circuit Visualization and Simulation Notes

Elec3D is primarily a graphics and visualization project. The circuit calculations are simplified and are meant to support visual feedback.

The current implementation:

- Treats components as graph nodes
- Treats connections as graph edges
- Assigns simple resistance values by component type
- Assigns batteries a default voltage
- Checks for disconnected components
- Checks whether a circuit loop exists
- Uses Eigen to solve looped portions of the circuit
- Displays voltage information in the UI

This is not a full SPICE-style simulator.

## Troubleshooting

### `cmake` is not recognized

Install CMake, then open a new terminal and run:

```powershell
cmake --version
```

### CMake cannot find a compiler on Windows

Install Visual Studio 2019 or 2022 with:

```text
Desktop development with C++
```

Then open a new PowerShell window and run:

```powershell
cmake -S . -B build
```

### `Failed to open layout.json`

You are probably running from the wrong folder.

Run like this:

```powershell
cd "C:\path\to\Elec3D\build"
.\bin\Debug\Elec3D.exe
```

Also confirm this file exists:

```text
src/layout.json
```

### `Failed to load shader files.`

Run from the `build` directory and confirm these files exist:

```text
shaders/cube.vert
shaders/cube.frag
```

### The window opens but the scene looks empty

Try these checks:

- Scroll the mouse wheel to zoom.
- Left-drag the mouse to rotate the camera.
- Make sure the default layout has components in `src/layout.json`.
- Make sure the visible layer is enabled in `Layer Control`.
- Check the terminal for layout or shader errors.

### Build errors mention missing headers

Confirm these folders exist:

```text
libs/
include/
```

The project expects bundled dependencies to remain in those folders.

## Clean Rebuild

Use this only when you want to regenerate the build directory from scratch.

From the project root on Windows:

```powershell
Remove-Item -Recurse -Force build
cmake -S . -B build
cmake --build build --config Debug
cd build
.\bin\Debug\Elec3D.exe
```

Warning: deleting `build` also deletes files generated inside it, including `build/output_layout.json`.

## Development Notes

- Main source file: `src/main.cpp`
- Default input layout: `src/layout.json`
- Shader files: `shaders/cube.vert` and `shaders/cube.frag`
- Generated executable directory: `build/bin`
- Build system: CMake
- Language standard: C++17
- OpenGL context requested by the app: OpenGL 3.3 core profile
