 
 ** How To Compile **

 ```
 cmake -Bbuild
 // or with ninja
 winget install Ninja-build.Ninja
 // debug
 cmake -S . -B Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
 cmake --build Debug
 // release
 cmake -S . -B Release -G Ninja -DCMAKE_BUILD_TYPE=Release
 cmake --build Release
 ```