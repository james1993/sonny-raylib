# Prebuilt Linux binary

`sonny-linux-x86_64` is the game built for 64-bit Linux. raylib is linked
into it; what it needs from the system is glibc, X11 and OpenGL.

The stage is the original's own 800 by 575 and every coordinate in the game is
in it, but that is an authored size: the window is resizable and the stage is
scaled into it, letterboxed to keep its shape, exactly as Flash scaled the
stage to whatever the player was given. It opens at as much of your monitor as
the stage's shape will take, up to four times the stage. **F11**, or
alt-enter, goes full screen.

The frame is drawn at the size the window actually is rather than at 800 by
575 and smoothed up to it, and the art is rasterised at twice the stage's own
resolution, so it stays sharp at any size your screen can show.

`SONNY_INFO=1` prints what the monitor, the window and the framebuffer came
out at, which is what to send if the window is not the size it should be.

Run it from the top of the repository, because it loads `assets/` relative to
the working directory:

    ./dist/sonny-linux-x86_64

or from anywhere, by telling it where the assets are:

    SONNY_ASSETS=/path/to/sonny-raylib ./dist/sonny-linux-x86_64

It was built against glibc 2.39 and needs 2.38 or newer (Ubuntu 24.04,
Debian 13, Fedora 39 and later). On anything older, build from source --
`make game` -- which is a better idea anyway if your distribution packages
raylib.

This binary is committed, so it goes stale whenever the game or the art
changes and is rebuilt with `make dist`. If it ever drifts from the art beside
it, it says so on startup and tells you to rebuild -- art rasterised finer
than a build expects draws at a multiple of its size, quietly, everywhere, and
that warning is the only thing that names it.
