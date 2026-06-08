@echo off
rem Build snake.com from the split sources.
rem NASM treats each %include as textual substitution, so the whole thing
rem still produces a single flat .com file.
pushd "%~dp0"
nasm -f bin snake.asm -o snake.com
if errorlevel 1 (
    echo Build failed.
    popd
    exit /b 1
)
echo Built %~dp0snake.com
popd
