# Neofcgi.v1
A simple fastcgi v1.1 protocol library with primary focus on extensibility and server features (like form-data requests).
The library does not support multiplexing.

The API structure for the library is not very similar to `libfcgi`. To get a reference on how to use the library look into the `tests/` folder, for the API reference look into `server.h`.

## Building
The library relies on premake5 for building. 
Use the `premake5.lua` in the upper folder to include the library in your project and use `premake5.lua` in the `tests/` folder to build the tests.

## Compatability and configuration
The library has been tested on linux and windows and could potentially be ported to other systems.
Most of the external calls the library makes can be configured through `conf.h`.

# `webrequest.html` file
I used this file to test the library, especially on windows.
On linux you will probably need to also get this file somewhere your server software will see it.
