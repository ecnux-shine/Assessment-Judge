#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <optional>
#include <map>

struct Limits {
    uint64_t timeMs = 1000;
    uint64_t memoryBytes = 256ull * 1024 * 1024;
};

struct RunResult {
    bool timedOut = false;
    bool memExceeded = false;
    bool launched = false;
    int exitCode = 0;
    uint64_t elapsedMs = 0;
    uint64_t peakMemoryBytes = 0;
    std::string output;
    std::string error;
};

static std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &result[0], sizeNeeded);
    return result;
}

static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], sizeNeeded, NULL, NULL);
    return result;
}

static std::string quoteCommandLineArg(const std::string& arg) {
    bool needQuote = arg.find_first_of(" \t\"\n") != std::string::npos;
    if (!needQuote) return arg;
    std::string quoted = "\"";
    for (char c : arg) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += '"';
    return quoted;
}

static std::string buildCommandLine(const std::string& target, const std::vector<std::string>& args) {
    std::string line = quoteCommandLineArg(target);
    for (const auto& arg : args) {
        line += ' ';
        line += quoteCommandLineArg(arg);
    }
    return line;
}

static bool loadConfig(Limits& limits) {
    std::ifstream in("aj_config.txt");
    if (!in) return false;
    std::string key;
    while (in >> key) {
        if (key == "time") {
            uint64_t value = 0;
            if (in >> value) {
                limits.timeMs = value;
            }
        } else if (key == "memory") {
            uint64_t value = 0;
            if (in >> value) {
                limits.memoryBytes = value * 1024 * 1024;
            }
        }
    }
    return true;
}

static bool saveConfig(const Limits& limits) {
    std::ofstream out("aj_config.txt", std::ios::trunc);
    if (!out) return false;
    out << "time " << limits.timeMs << "\n";
    out << "memory " << (limits.memoryBytes / (1024 * 1024)) << "\n";
    return true;
}

static void readPipeToOutput(HANDLE pipe, std::ostream* outputStream, std::string* capture, std::atomic<bool>& stopFlag) {
    const DWORD bufferSize = 4096;
    char buffer[bufferSize];
    DWORD bytesRead = 0;
    while (!stopFlag.load()) {
        BOOL ok = ReadFile(pipe, buffer, bufferSize, &bytesRead, NULL);
        if (!ok || bytesRead == 0) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                break;
            }
            break;
        }
        if (bytesRead > 0) {
            std::string chunk(buffer, bytesRead);
            if (outputStream) {
                *outputStream << chunk;
                outputStream->flush();
            }
            if (capture) {
                capture->append(chunk);
            }
        }
    }
}

static size_t getPeakMemoryBytes(HANDLE process) {
    PROCESS_MEMORY_COUNTERS memInfo;
    if (GetProcessMemoryInfo(process, &memInfo, sizeof(memInfo))) {
        return static_cast<size_t>(memInfo.PeakWorkingSetSize);
    }
    return 0;
}

static RunResult runProcess(
    const std::string& target,
    const std::vector<std::string>& targetArgs,
    const std::string& stdinData,
    const Limits& limits,
    bool interactiveInput,
    bool captureOutput,
    bool captureError,
    bool printChildOutput
) {
    RunResult result;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE childStdInRead = NULL;
    HANDLE childStdInWrite = NULL;
    HANDLE childStdOutRead = NULL;
    HANDLE childStdOutWrite = NULL;
    HANDLE childStdErrRead = NULL;
    HANDLE childStdErrWrite = NULL;

    if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &sa, 0)) {
        return result;
    }
    if (!SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
        return result;
    }
    if (!CreatePipe(&childStdErrRead, &childStdErrWrite, &sa, 0)) {
        return result;
    }
    if (!SetHandleInformation(childStdErrRead, HANDLE_FLAG_INHERIT, 0)) {
        return result;
    }
    if (!CreatePipe(&childStdInRead, &childStdInWrite, &sa, 0)) {
        return result;
    }
    if (!SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
        return result;
    }

    PROCESS_INFORMATION pi;
    STARTUPINFOW si;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childStdInRead;
    si.hStdOutput = childStdOutWrite;
    si.hStdError = childStdErrWrite;

    std::string commandLine = buildCommandLine(target, targetArgs);
    std::wstring commandLineW = utf8ToWide(commandLine);
    std::vector<wchar_t> cmdLineBuf(commandLineW.begin(), commandLineW.end());
    cmdLineBuf.push_back(0);

    BOOL ok = CreateProcessW(
        NULL,
        cmdLineBuf.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(childStdInRead);
    CloseHandle(childStdOutWrite);
    CloseHandle(childStdErrWrite);

    if (!ok) {
        return result;
    }

    result.launched = true;
    std::atomic<bool> stopReaders(false);
    std::string childOutput;
    std::string childError;

    std::thread stdoutThread;
    std::thread stderrThread;
    std::ostream* stdoutPtr = (printChildOutput || captureOutput) ? &std::cout : nullptr;
    std::ostream* stderrPtr = (printChildOutput || captureError) ? &std::cerr : nullptr;
    
    stdoutThread = std::thread(readPipeToOutput, childStdOutRead, stdoutPtr, captureOutput ? &childOutput : nullptr, std::ref(stopReaders));
    stderrThread = std::thread(readPipeToOutput, childStdErrRead, stderrPtr, captureError ? &childError : nullptr, std::ref(stopReaders));

    std::atomic<bool> processEnded(false);
    std::atomic<bool> timeLimitExceeded(false);
    std::atomic<bool> memoryLimitExceeded(false);
    std::atomic<size_t> peakMemory(0);
    std::atomic<bool> terminatedByLimit(false);

    auto startTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point pauseStart;
    uint64_t pausedMs = 0;
    bool paused = false;
    auto pauseTimer = [&]() {
        if (!paused) {
            pauseStart = std::chrono::steady_clock::now();
            paused = true;
        }
    };
    auto resumeTimer = [&]() {
        if (paused) {
            auto now = std::chrono::steady_clock::now();
            pausedMs += std::chrono::duration_cast<std::chrono::milliseconds>(now - pauseStart).count();
            paused = false;
        }
    };
    auto getElapsedMs = [&]() {
        auto now = std::chrono::steady_clock::now();
        uint64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (paused) {
            elapsed -= std::chrono::duration_cast<std::chrono::milliseconds>(now - pauseStart).count();
        }
        if (elapsed > pausedMs) {
            elapsed -= pausedMs;
        } else {
            elapsed = 0;
        }
        return elapsed;
    };

    std::thread monitorThread([&]() {
        while (!processEnded.load()) {
            size_t currentPeak = getPeakMemoryBytes(pi.hProcess);
            if (currentPeak > peakMemory.load()) {
                peakMemory.store(currentPeak);
            }
            if (limits.memoryBytes > 0 && currentPeak > limits.memoryBytes) {
                memoryLimitExceeded.store(true);
                terminatedByLimit.store(true);
                TerminateProcess(pi.hProcess, 1);
                break;
            }
            if (limits.timeMs > 0 && getElapsedMs() > limits.timeMs) {
                timeLimitExceeded.store(true);
                terminatedByLimit.store(true);
                TerminateProcess(pi.hProcess, 1);
                break;
            }
            Sleep(10);
        }
    });

    if (!interactiveInput) {
        if (!stdinData.empty()) {
            DWORD written = 0;
            WriteFile(childStdInWrite, stdinData.data(), (DWORD)stdinData.size(), &written, NULL);
        }
        CloseHandle(childStdInWrite);
    }

    if (interactiveInput) {
        while (true) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 0);
            if (wait == WAIT_OBJECT_0) break;
            pauseTimer();
            std::string line;
            if (!std::getline(std::cin, line)) {
                break;
            }
            resumeTimer();
            line.push_back('\n');
            DWORD written = 0;
            WriteFile(childStdInWrite, line.data(), (DWORD)line.size(), &written, NULL);
        }
        CloseHandle(childStdInWrite);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    processEnded.store(true);
    if (paused) {
        resumeTimer();
    }
    result.elapsedMs = getElapsedMs();
    result.peakMemoryBytes = peakMemory.load();
    if (result.peakMemoryBytes == 0) {
        result.peakMemoryBytes = getPeakMemoryBytes(pi.hProcess);
    }
    result.timedOut = timeLimitExceeded.load();
    result.memExceeded = memoryLimitExceeded.load();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = static_cast<int>(exitCode);

    stopReaders.store(true);
    CloseHandle(childStdOutRead);
    CloseHandle(childStdErrRead);
    if (stdoutThread.joinable()) stdoutThread.join();
    if (stderrThread.joinable()) stderrThread.join();
    if (monitorThread.joinable()) monitorThread.join();

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(childStdInWrite);

    result.output = childOutput;
    result.error = childError;

    return result;
}

static std::vector<std::string> splitTokens(const std::string& content) {
    std::vector<std::string> tokens;
    std::istringstream iss(content);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

static std::string readFileContent(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string formatMemory(uint64_t bytes) {
    double mb = bytes / 1024.0 / 1024.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << mb;
    return ss.str();
}

static void printUsage() {
    std::cout << "Assessment Judge (aj) usage:\n";
    std::cout << "  aj.exe program.exe\n";
    std::cout << "  aj.exe program.exe -i \"input content\"\n";
    std::cout << "  aj.exe program.exe -f \"tests.txt\"\n";
    std::cout << "  aj.exe program.exe -f \"tests.txt\" -a\n";
    std::cout << "  aj.exe program.exe -ct 200 -cm 200 -i \"input\"\n";
    std::cout << "  aj.exe -c time 200 memory 200\n";
    std::cout << "Default limits: time=1000ms, memory=256MB.\n";
}

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printUsage();
        return 1;
    }

    Limits limits;
    loadConfig(limits);
    std::optional<std::string> targetExe;
    std::string inlineInput;
    std::string filePath;
    bool allDetails = false;
    bool configOnly = false;
    bool changeDefaults = false;
    bool useInteractive = false;
    bool setCustomTime = false;
    bool setCustomMemory = false;
    int64_t customTimeMs = 0;
    int64_t customMemoryMb = 0;
    int explicitGroupSize = 0;

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "-c") {
            configOnly = true;
            changeDefaults = true;
        } else if (arg == "time" && configOnly) {
            if (i + 1 < argc) {
                limits.timeMs = std::stoull(argv[++i]);
            }
        } else if (arg == "memory" && configOnly) {
            if (i + 1 < argc) {
                limits.memoryBytes = std::stoull(argv[++i]) * 1024 * 1024;
            }
        } else if (arg == "-ct") {
            if (i + 1 < argc) {
                setCustomTime = true;
                customTimeMs = std::stoll(argv[++i]);
            }
        } else if (arg == "-cm") {
            if (i + 1 < argc) {
                setCustomMemory = true;
                customMemoryMb = std::stoll(argv[++i]);
            }
        } else if (arg == "-i") {
            if (i + 1 < argc) {
                inlineInput = argv[++i];
            }
        } else if (arg == "-f") {
            if (i + 1 < argc) {
                filePath = argv[++i];
            }
        } else if (arg == "-a") {
            allDetails = true;
        } else if (arg == "-g") {
            if (i + 1 < argc) {
                explicitGroupSize = std::stoi(argv[++i]);
            }
        } else if (arg.rfind("-", 0) == 0) {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            if (!targetExe.has_value()) {
                targetExe = arg;
            } else {
                args.push_back(arg);
            }
        }
    }

    if (changeDefaults && targetExe.has_value()) {
        std::cerr << "The -c option must be used alone.\n";
        return 1;
    }

    if (setCustomTime) limits.timeMs = customTimeMs;
    if (setCustomMemory) limits.memoryBytes = customMemoryMb * 1024 * 1024;

    if (changeDefaults) {
        if (!saveConfig(limits)) {
            std::cerr << "Could not save configuration file.\n";
            return 1;
        }
        std::cout << "Default limits updated: time=" << limits.timeMs << "ms memory=" << (limits.memoryBytes / (1024 * 1024)) << "MB\n";
        return 0;
    }

    if (!targetExe.has_value()) {
        std::cerr << "Target program is required.\n";
        printUsage();
        return 1;
    }

    if (!filePath.empty() && !inlineInput.empty()) {
        std::cerr << "Cannot use both -i and -f at the same time.\n";
        return 1;
    }

    if (!filePath.empty()) {
        std::string fileContent = readFileContent(filePath);
        if (fileContent.empty()) {
            std::cerr << "Could not read input file or file is empty.\n";
            return 1;
        }
        std::vector<std::string> tokens = splitTokens(fileContent);
        if (tokens.empty()) {
            std::cerr << "No tokens found in test file.\n";
            return 1;
        }

        int groupSize = explicitGroupSize;
        if (groupSize <= 0) {
            int maxTry = (int)std::min<size_t>(10, tokens.size());
            for (int trySize = 1; trySize <= maxTry; ++trySize) {
                std::string groupInput;
                for (int idx = 0; idx < trySize; ++idx) {
                    if (idx) groupInput += " ";
                    groupInput += tokens[idx];
                }
                RunResult probe = runProcess(targetExe.value(), args, groupInput, limits, false, true, true, false);
                if (!probe.launched) continue;
                if (!probe.timedOut && !probe.memExceeded && probe.exitCode == 0) {
                    groupSize = trySize;
                    break;
                }
            }
            if (groupSize <= 0) {
                groupSize = (int)tokens.size();
            }
        }

        int totalGroups = (int)tokens.size() / groupSize;
        int remainder = (int)tokens.size() % groupSize;
        std::map<std::string, int> stats;
        uint64_t totalTime = 0;
        uint64_t totalMemory = 0;
        int executedRuns = 0;

        for (int groupIndex = 0; groupIndex < totalGroups; ++groupIndex) {
            std::string groupInput;
            for (int j = 0; j < groupSize; ++j) {
                if (j) groupInput += " ";
                groupInput += tokens[groupIndex * groupSize + j];
            }
            if (allDetails) {
                std::cout << "##########[" << (groupIndex + 1) << "]##########\n\n";
            }
            RunResult run = runProcess(targetExe.value(), args, groupInput, limits, false, false, false, true);
            executedRuns += 1;
            totalTime += run.elapsedMs;
            totalMemory += run.peakMemoryBytes;
            std::string status;
            if (run.timedOut) {
                status = "TEL";
            } else if (run.memExceeded) {
                status = "MLE";
            } else if (run.exitCode != 0) {
                status = "WA";
            } else {
                status = "AC";
            }
            stats[status] += 1;
            if (allDetails) {
                std::cout << "Status: " << status << "\n";
                std::cout << "Time: " << run.elapsedMs << "ms\n";
                std::cout << "Memory: " << formatMemory(run.peakMemoryBytes) << " MB\n";
                std::cout << "\n";
            }
        }

        if (remainder > 0) {
            stats["WA"] += 1;
        }

        if (!allDetails) {
            if (executedRuns > 0) {
                uint64_t avgTime = totalTime / executedRuns;
                uint64_t avgMemory = totalMemory / executedRuns;
                std::cout << "Average Time: " << avgTime << "ms\n";
                std::cout << "Average Memory: " << formatMemory(avgMemory) << " MB\n";
            } else {
                std::cout << "Average Time: 0ms\n";
                std::cout << "Average Memory: 0.0 MB\n";
            }
            int totalCount = executedRuns + (remainder > 0 ? 1 : 0);
            std::cout << "All Statuses: ";
            std::cout << "AC: " << stats["AC"] << "/" << totalCount << "      ";
            std::cout << "TEL: " << stats["TEL"] << "/" << totalCount << "      ";
            std::cout << "WA: " << stats["WA"] << "/" << totalCount << "      ";
            std::cout << "MLE: " << stats["MLE"] << "/" << totalCount << "\n";
        }

        return 0;
    }

    if (!inlineInput.empty()) {
        RunResult run = runProcess(targetExe.value(), args, inlineInput, limits, false, false, false, true);
        std::cout << "------------------------------\n";
        if (run.timedOut) {
            std::cout << "Status: TEL\n";
        } else if (run.memExceeded) {
            std::cout << "Status: MLE\n";
        } else if (run.exitCode != 0) {
            std::cout << "Status: WA\n";
        } else {
            std::cout << "Time: " << run.elapsedMs << " ms\n";
            std::cout << "Memory: " << formatMemory(run.peakMemoryBytes) << " MB\n";
            std::cout << "Status: AC\n";
        }
        return 0;
    }

    std::cout << "------------------------------\n";
    RunResult run = runProcess(targetExe.value(), args, std::string(), limits, true, false, false, true);
    if (run.timedOut) {
        std::cout << "Status: TEL\n";
    } else if (run.memExceeded) {
        std::cout << "Status: MLE\n";
    } else if (run.exitCode != 0) {
        std::cout << "Status: WA\n";
    } else {
        std::cout << "Time: " << run.elapsedMs << " ms\n";
        std::cout << "Memory: " << formatMemory(run.peakMemoryBytes) << " MB\n";
        std::cout << "Status: AC\n";
    }
    return 0;
}
