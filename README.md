# Assessment Judge (AJ)

English | [简体中文](README_CN.md)

A Windows C++ command-line tool to run `.exe` programs under a simulated terminal environment and report execution time and memory usage.

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
aj.exe program.exe -f tests.txt
```

Output all detailed results per case:

```bat
aj.exe program.exe -f tests.txt -a
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
- Multi-data mode uses blank lines to separate test cases.
- Each test case may contain multiple lines of input.
- If the test data file leaves an incomplete final case, it is counted as a WA (Wrong Answer)
