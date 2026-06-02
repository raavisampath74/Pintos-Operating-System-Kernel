# Pintos Operating System - PA2

This repository contains the source code for the Pintos Operating System, specifically focusing on Programming Assignment 2 (User Programs). 

## Overview

Pintos is a simple operating system framework for the 80x86 architecture. It supports kernel threads, loading and running user programs, and a file system, but it implements all of these in a very simple way. 

The `src` directory contains the source code for the various modules of Pintos:
- `threads/`: Basic thread management and synchronization.
- `userprog/`: Loading and executing user programs.
- `vm/`: Virtual memory (for later assignments).
- `filesys/`: Basic file system.
- `devices/`: I/O device interfacing (keyboard, timer, disk, etc.).
- `lib/`: Standard library functions implementation.
- `utils/`: Utility programs used for building and managing Pintos.

## Building and Running

You can use the provided `Makefile` inside the `src` directory to build the OS and run tests.

```sh
cd src/userprog
make
make check
```
