# Assessment Judge (AJ)

A Windows C++ command-line tool to run `.exe` programs under a simulated terminal environment and report time/memory usage.

## Build

Use a compatible compiler on Windows. If using MinGW or MSYS2:

```bat
g++ aj.cpp -std=c++17 -O2 -o aj.exe -lpsapi
```

If using MSVC Developer Command Prompt:

```bat
cl /EHsc aj.cpp psapi.lib
```

## Usage

Manual mode:

```bat
aj.exe program.exe
```

Input test mode:

```bat
aj.exe program.exe -i "input content"
```

Multi-data test mode:

```bat
aj.exe program.exe -f "tests.txt"
```

Output all detailed results per case:

```bat
aj.exe program.exe -f "tests.txt" -a
```

Override limits for a single run:

```bat
aj.exe program.exe -ct 200 -cm 200 -i "input"
```

Save default limits:

```bat
aj.exe -c time 200 memory 200
```

## Notes

- Default time limit: `1000ms`
- Default memory limit: `256MB`
- Manual mode pauses timing while waiting for user input.
- Multi-data mode splits test file contents by whitespace and runs repeated cases.
- If the test data file leaves an incomplete final case, it is counted as a WA.
