# Rconpanel for Digital Paint: Paintball 2 servers
TODO: Description
TODO: Screenshots (remake?)

## Building

Use the provided project file for Visual Studio. In the past, I had success building using these commands with MinGW-G++:

```
mkdir build

g++.exe --std=c++20 -Os -c main.cpp -o build\main.o
g++.exe --std=c++20 -Os -c rconfunctions.cpp -o build\rconfunctions.o
windres.exe -J rc -O coff -i resource.rc -o "build\resource.res"
g++.exe -o"build\Rconpanel.exe build\main.o build\rconfunctions.o build\resource.res -Os -s -lshlwapi -mwindows
```