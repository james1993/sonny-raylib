# Prebuilt Linux binary

`sonny-linux-x86_64` is the game built for 64-bit Linux. raylib is linked
into it; what it needs from the system is glibc, X11 and OpenGL.

The stage is the original's own 800 by 575 and every coordinate in the game is
in it, but that is an authored size: the window is resizable and the stage is
scaled into it, letterboxed to keep its shape, exactly as Flash scaled the
stage to whatever the player was given. It opens at the largest whole multiple
that leaves room on your monitor -- two or three up on most screens. **F11**,
or alt-enter, goes full screen.

Run it from the top of the repository, because it loads `assets/` relative to
the working directory:

    ./dist/sonny-linux-x86_64

or from anywhere, by telling it where the assets are:

    SONNY_ASSETS=/path/to/sonny-raylib ./dist/sonny-linux-x86_64

It was built against glibc 2.39 and needs 2.38 or newer (Ubuntu 24.04,
Debian 13, Fedora 39 and later). On anything older, build from source --
`make game` -- which is a better idea anyway if your distribution packages
raylib.
