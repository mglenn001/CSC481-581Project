# CSC481-581Project
The Game Engine Construction Project is designed to help you gain comprehensive, hands-on experience in the design, development, integration, and testing of a functional game engine. You will also implement individual games using your team engine to showcase system functionalities. This project allows you to develop and demonstrate competencies in game engine architecture, system implementation, and problem-solving as applied to real-world game development scenarios.

## Team 8

Sabah Afroz, safroz

Mia Glenn, mglenn2

Tram Nguyen, tnnguy22

##  Setup Instructions

Follow these steps to clone, build, and run the project on your machine:

### 1. Clone the repository

```bash
git clone https://github.com/mglenn001/CSC481-581Project.git
```

----------

### 2. Install SDL3

> SDL3 is the latest major release. You’ll need both SDL3 and optionally `SDL3_image` if the sprite sheet is in PNG format.

#### On **macOS** (with Homebrew):

```bash
brew install sdl3

```

#### On **Ubuntu/Debian**:

```bash
sudo apt install libsdl3-dev

```

#### On **Arch Linux**:

```bash
sudo pacman -S sdl3

```

#### On **Windows**:

Use [MSYS2](https://www.msys2.org/) or [vcpkg](https://vcpkg.io/) for easiest installation.

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL3

```

Then launch the MinGW64 shell and continue.

----------

### 3. Build the project

```bash
mkdir build && cd build
cmake ..
make

```

> Ensure `SDL3_DIR` is correctly set if CMake cannot find SDL.

----------

### 5. Run the program

#### On the **first terminal**:

```bash
./server

```

#### On the **second terminal**:

```bash
./main 1

```

#### On the **third terminal**:

```bash
./main 2

```

#### On the **fourth terminal**:

```bash
./main 3

```

You should see client/server running in the console and Game Engine window(s) open.

## Game Control

* **A** — Move Left
* **D** — Move Right
* **W** — Jump
* **Shift** — Hold to Run
* **T** — Toggle Resolution Scaling (Proportional vs. Pixel)
* **P** — Pause / Unpause
* **+ / -** — Increase / Decrease game speed