if exist ..\emsdk\emsdk_env.bat call ..\emsdk\emsdk_env

::: emcc zippast.c
::: node a.out.js

::: -s EXPORTED_FUNCTIONS=["_run"]

::: -O2

emcc zippast.c -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_RUNTIME_METHODS=["callMain"] -o docs/zippast.html
