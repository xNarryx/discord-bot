@echo off
chcp 65001 >nul
setlocal

cd /d "%~dp0"

set EXE=C:\Users\nazar\source\repos\дс жопа бота\x64\Debug\дс жопа бота.exe

:loop
echo [INFO] Запуск "%EXE%"...
"%EXE%"
echo [WARN] %EXE% завершился. Перезапуск через 3 секунды...
timeout /t 3 >nul
goto loop
