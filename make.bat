mkdir build

g++.exe -std=c++11 -fno-rtti -ffunction-sections -fdata-sections -Wl,--gc-sections -fomit-frame-pointer -fexpensive-optimizations -Os -IC:\boost_1_54_0\ -c main.cpp -o "build\main.o"
g++.exe -std=c++11 -fno-rtti -ffunction-sections -fdata-sections -Wl,--gc-sections -fomit-frame-pointer -fexpensive-optimizations -Os -IC:\boost_1_54_0\ -c rconfunctions.cpp -o "build\rconfunctions.o"
windres.exe  -J rc -O coff -i resource.rc -o "build\resource.res"
g++.exe  -o "build\Rconpanel.exe" "build\main.o" "build\rconfunctions.o"  "build\resource.res" -Os -Wl,--gc-sections -s  -lshlwapi -lwininet -lws2_32 -lgdi32 -lole32 -lcomctl32 C:\boost_1_54_0\bin.v2\libs\regex\build\gcc-mingw-4.8.1\release\link-static\threading-multi\libboost_regex-mgw48-mt-1_54.a -mwindows

del "build\main.o"
del "build\rconfunctions.o"
del "build\resource.res"