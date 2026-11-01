
## Development Environment

- Language: C11 / GNU C11
- OS: Ubuntu 24.04
- IDE: CLion
- Compiler: GCC
- Build System: CMake
- Debugger: GDB

## Setup

1. Clone the GitHub repository.
2. Install Docker Desktop.
3. Build the development image:

   docker build -t cs3103-clion-linux:1.0 -f Dockerfile.clion .

4. Open the project in CLion.
5. Configure a Docker Toolchain using the image.
6. Create a CMake Profile called Docker-Debug.
7. Use the Docker-Debug profile to build and run.

## Targets

- problem1: Multi-Stage Producer-Consumer
- minifs: Mini File System Simulator
- problem3: CPU Scheduling

## Build on Linux

```bash
make
```

## Run

### Problem 1
```bash
./problem1
```

### Problem 2
```bash
./minifs
```

### Problem 3
```bash
./problem3 test_cases/q3/case1.txt
```
