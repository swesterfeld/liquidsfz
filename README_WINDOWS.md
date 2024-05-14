# Building liquidsfz under Windows

## Install a suitable compiler

Compiling was tested with gcc using the online installer from

 * https://sourceforge.net/projects/mingw-w64/files

Within the installer, select

 * version `8.1.0`
 * architecture `x86_64`
 * threads `posix`
 * exception `seh`
 * build revision `0`

It is likely that other compilers could also work. If you need changes to build
with another compiler, please report it as a GitHub issue.

## cmake

We assume that cmake is also installed and working, and in the PATH.

## Preparing the liquidsfz source code

The source code can be downloaded from GitHub. For this document we assume that
the folder containing the master branch of liquidsfz was extracted to
`C:\src\liquidsfz-master`

Since cmake support is still experimental, the file in the repository is called
`testCMakeLists.txt`. You need to rename it before building:

    C:\src\liquidsfz-master> ren testCMakeLists.txt CMakeLists.txt

## Installing libsndfile

Also from GitHub, you can get it from here:

* https://github.com/libsndfile/libsndfile/releases/download/v1.0.30/libsndfile-1.0.30-win64.zip

Extract the folder so that there is a new subfolder: `C:\src\liquidsfz-master\libsndfile-1.0.30-win64`

## Building

Run `mingw-w64.bat`, in a typical installation

    C:\Program Files\mingw-w64\x86_64-8.1.0-posix-seh-rt_p6-rev0\mingw-v64.bat

To build, go to the liquidsfz source folder and use the following commands

    C:\src\liquidsfz-master> mkdir build
    C:\src\liquidsfz-master> cd build
    C:\src\liquidsfz-master\build> cmake -G"MinGW Makefiles" .. -DCMAKE_PREFIX_PATH=C:\src\liquidsfz-master\libsndfile-1.0.30-win64
    C:\src\liquidsfz-master\build> mingw32-make
