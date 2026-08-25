@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /utf-8 /O2 /DSQLITE_THREADSAFE=1 /DSQLITE_OMIT_LOAD_EXTENSION /DSQLITE_DQS=0 /DSQLITE_DEFAULT_MEMSTATUS=0 ^
  /Fe:Server.exe ^
  Server\Server.cpp Server\ChatLog.cpp Server\Accounts.cpp Server\Crypto.cpp Server\Rooms.cpp Server\Session.cpp ^
  Server\Friends.cpp Server\DirectMessages.cpp Server\NatPortMapping.cpp ^
  Shared\Framing.cpp ThirdParty\sqlite\sqlite3.c ^
  /IShared /IServer /IThirdParty\sqlite ^
  ws2_32.lib
