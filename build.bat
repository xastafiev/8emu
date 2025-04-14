if not defined DevEnvDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
)

cl /std:c++20 /DUNICODE /D_UNICODE /Zi /I.\include main.cpp /link user32.lib Gdi32.lib Winmm.lib
