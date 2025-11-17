# Building Astonia Community Client

This document describes how to build the Astonia Community Client for Windows.

## Table of Contents

- [Building on Windows (Native)](#building-on-windows-native)
- [Cross-Compiling from Linux (Docker)](#cross-compiling-from-linux-docker)
- [Build Outputs](#build-outputs)
- [Troubleshooting](#troubleshooting)

---

## Building on Windows (Native)

### Prerequisites

1. **MSYS2** - Download and install from https://www.msys2.org/
2. Open **MSYS2 MinGW64** terminal (not MSYS2 MSYS)

### Install Dependencies

```bash
pacman -S mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-SDL2 \
          mingw-w64-x86_64-SDL2_mixer \
          mingw-w64-x86_64-SDL2_ttf \
          mingw-w64-x86_64-libpng \
          mingw-w64-x86_64-zlib \
          mingw-w64-x86_64-libzip \
          mingw-w64-x86_64-dwarfstack \
          make
```

### Build

```bash
# Build main executable
make

# Build additional tools and DLL
make amod convert anicopy

# Create distribution package with DLLs
make distrib
```

This will create `windows_client.zip` containing all executables and required DLLs.

---

## Cross-Compiling from Linux (Docker)

The easiest way to build Windows executables from Linux is using Docker. This approach:
- Keeps your system clean (no MinGW installation required)
- Provides reproducible builds
- Works on any Linux distribution

### Prerequisites

- Docker installed and running
- Linux system (tested on Arch Linux)

### Quick Start

#### Option 1: Using the Build Script

```bash
# Make script executable (first time only)
chmod +x docker-build.sh

# Build Windows executables
./docker-build.sh
```

The script will:
1. Build the Docker image (downloads Fedora + MinGW toolchain)
2. Compile all Windows executables
3. Place outputs in `bin/` directory

#### Option 2: Using Make Targets

```bash
# Build Docker image and compile project
make docker-build

# Build and create distribution package
make docker-distrib

# Just build the Docker image
make docker-image
```

#### Option 3: Manual Docker Commands

```bash
# Build Docker image
docker build -t astonia-windows-builder .

# Compile the project
docker run --rm -v "$(pwd):/workspace" -u "$(id -u):$(id -g)" astonia-windows-builder

# Build with all tools
docker run --rm -v "$(pwd):/workspace" -u "$(id -u):$(id -g)" \
  astonia-windows-builder \
  bash -c "make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres && \
           make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres amod convert anicopy"
```

### How It Works

The Docker build uses:
- **Base Image**: Fedora 39 (excellent MinGW package support)
- **Compiler**: MinGW-w64 GCC (x86_64-w64-mingw32-gcc)
- **Dependencies**: Pre-built MinGW versions of SDL2, SDL2_mixer, SDL2_ttf, libpng, zlib, libzip

The Dockerfile installs all dependencies and sets up the cross-compilation environment automatically.

---

## Build Outputs

After a successful build, you'll find the following in the `bin/` directory:

| File | Description |
|------|-------------|
| `moac.exe` | Main Astonia client executable (1.8 MB) |
| `amod.dll` | Modding DLL for external modifications (95 KB) |
| `convert.exe` | Helper utility for data conversion (139 KB) |
| `anicopy.exe` | Animation copying utility (101 KB) |

All files are **64-bit Windows PE executables** (PE32+).

### Verifying Build

```bash
# Check executables were created
ls -lh bin/*.exe bin/*.dll

# Verify they're Windows executables
file bin/moac.exe
# Output: PE32+ executable for MS Windows 5.02 (GUI), x86-64, 23 sections
```

---

## Troubleshooting

### Common Issues

#### "Cannot find -ldwarfstack" (Linux/Docker builds)

**Cause**: The dwarfstack library is only available in MSYS2, not in Fedora's MinGW packages.

**Solution**: Already handled! The code has been updated to make dwarfstack optional. Docker builds will compile without it and show "Stack trace not available (built without dwarfstack)" on crashes.

To enable dwarfstack on Windows MSYS2 builds:
```bash
make LIBS_OPTIONAL=-ldwarfstack
```

#### "Permission denied" when running docker-build.sh

**Solution**:
```bash
chmod +x docker-build.sh
```

#### Docker build fails with "no space left on device"

**Solution**: Clean up Docker images and containers:
```bash
docker system prune -a
```

#### Modified files show wrong ownership after Docker build

**Cause**: Docker runs as root by default, but we use `-u "$(id -u):$(id -g)"` to match your user.

**Solution**: The build script already handles this. If files have wrong ownership:
```bash
sudo chown -R $(id -u):$(id -g) bin/
```

---

## Testing with Wine on Linux

After building with Docker, you can test the Windows executable on Linux using Wine:

### 1. Copy Required DLLs

First, copy all Windows DLLs from the Docker container:

```bash
./copy-dlls.sh
```

This copies 14 DLLs (~8 MB total):
- SDL2, SDL2_mixer, SDL2_ttf (graphics/audio)
- libpng, zlib, libzip, libbz2 (file handling)
- libfreetype, libharfbuzz (font rendering)
- Runtime libraries (gcc, pthread, glib, etc.)

### 2. Run with Wine

```bash
wine bin/moac.exe -u username -p password -d server.com -k 30 -r 1080p
```

For actual gameplay, it's recommended to run on a real Windows system.
