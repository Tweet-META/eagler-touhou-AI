@echo off
setlocal
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js was not found. Please install Node.js and try again.
  pause
  exit /b 1
)
if not exist "web\runtime\avoidance.wasm" (
  echo WebAssembly module is missing. See README.md for build instructions.
  pause
  exit /b 1
)
echo AI demo: http://127.0.0.1:8144/
echo Keep this window open while using the demo.
echo Open the address above in your browser.
node scripts/serve.mjs 8144
pause
