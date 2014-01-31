##What is this?##

A cross-platform collection of containers and system-access utilities (for C++) that I ripped out of the the llvm project and modified a bit to use in personal projects. It was originally from llvm v3.4.

I pared down the configuration options quite a bit and streamlined the build process: it now requires a c++11 capable compiler, and expects all the system libraries to be in standard locations. The advantage of this is that there is no build configuration required at all.

Look at the [LLVM Programmer's Manual](http://llvm.org/docs/ProgrammersManual.html#picking-the-right-data-structure-for-a-task "LLVM Programmer's manual") for some documentation on the various container classes.

##Why?##

Ease of cross-platform application development. I mostly wanted access to `StringRef`+`Twine` (lightweight buffer-reference classes that can often act as nearly transparent and allocation free replacements for `std::string`), `File[In/Out]putBuffer` (cross platform memory mapped files), `RawOstream` (cross platform colored terminal output), and the rest of it came in as a ball of dependencies.

I also wanted to ease the pain of constantly fiddling with build systems as much as possible.

##Building##
Just aim your compiler at `build-all.cpp`. 
That's it.

(With gcc and clang make sure to use `-std=c++11`. And you're responsible for linking in any required system libs.)

I've tested this on Ubuntu 13.04 with gcc and clang, and on Windows with VS2013 and VS2012 (with the v120 compiler update). I think other Linuxes and OSX should work without major issues as well. 
