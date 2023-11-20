<div align=center>

The JIT Compiler
================

</div>

This version of CPython can be built with an experimental just-in-time compiler. While most everything you already know about building and using CPython is unchanged, you will probably need to install a compatible version of LLVM first.

### Installing LLVM

While the JIT compiler does not require end users to install any third-party dependencies, part of it must be *built* using LLVM. It is *not* required for you to build the rest of CPython using LLVM, or the even the same version of LLVM (in fact, this is uncommon).

LLVM version 16 is required. Both `clang` and `llvm-readobj` need to be installed and discoverable (version suffixes, like `clang-16`, are okay). It's highly recommended that you also have `llvm-objdump` available, since this allows the build script to dump human-readable assembly for the generated code.

It's easy to install all of the required tools:

#### Linux

Install LLVM 16 on Ubuntu/Debian:

```sh
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 16
```

#### macOS

Install LLVM 16 with [Homebrew](https://brew.sh):

```sh
$ brew install llvm@16
```

Homebrew won't add any of the tools to your `$PATH`. That's okay; the build script knows how to find them.

#### Windows

LLVM 16 can be installed on Windows by using the installers published on [LLVM's GitHub releases page](https://github.com/llvm/llvm-project/releases/tag/llvmorg-16.0.6).

[Here's a recent one.](https://github.com/llvm/llvm-project/releases/download/llvmorg-16.0.6/LLVM-16.0.6-win64.exe) **When installing, be sure to select the option labeled "Add LLVM to the system PATH".**

### Building

For `PCbuild`-based builds, pass the new `--experimental-jit` option to `build.bat`.

For all other builds, pass the new `--enable-experimental-jit` option to `configure`.

Otherwise, just configure and build as you normally would. Even cross-compiling "just works", since the JIT is built for the host platform.
