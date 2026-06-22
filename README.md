# Assessment Judge (AJ)

A Windows C++ command-line tool to run `.exe` programs under a simulated terminal environment and report execution time and memory usage.

## Build

### Option 1: MSVC (Visual Studio)

Open **Developer Command Prompt for Visual Studio** and run:

```batch
cd path\to\AssessmentJudge
rc /fo assets\resource.res assets\resource.rc
cl /EHsc aj.cpp assets\resource.res psapi.lib
```

This will generate `aj.exe` with the favicon embedded.

### Option 2: MinGW/GCC (MSYS2 or standalone)

Run in your shell:

```bash
cd path/to/AssessmentJudge
windres assets/resource.rc -o assets/resource.o
g++ aj.cpp assets/resource.o -std=c++17 -O2 -o aj.exe -lpsapi
```

This will generate `aj.exe` with the favicon embedded.

## Usage

Manual mode (interactive input):

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

Save default limits (this must be used alone):

```bat
aj.exe -c time 200 memory 200
```

## Notes

- Default time limit: `1000 ms` (1 second)
- Default memory limit: `256 MB`
- Manual mode pauses timing while waiting for user input
- Multi-data mode splits test file contents by whitespace and runs repeated cases
- If the test data file leaves an incomplete final case, it is counted as a WA (Wrong Answer)
