# SSP.CHAT
A simple chat in a linux terminal.

The code is written in pure C and uses POSIX-compatible system calls (pthread, socket, select). This makes it incredibly portable to any system that has:

1. The standard C library (libc).

2. The POSIX thread implementation (pthread).

3. The TCP/IP network stack (socket support).
