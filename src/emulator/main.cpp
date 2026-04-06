/**
 * main.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Emulator entry point.
 *
 * Usage:
 *   gbemu <rom.gb> [save.sav]
 *
 * This stub creates a headless GameBoy instance for now.  To attach a real
 * display, implement IDisplay (e.g. using SDL2) and pass it to the GameBoy
 * constructor.  An SDL2 frontend example is in docs/SDL2Frontend.md.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#include "GameBoy.hpp"
#include "debug/Debugger.hpp"
#include "savestate/SaveState.hpp"

#ifdef GB_SDL2_BACKEND
#include "../frontend/SDL2Display.hpp"
#include "../frontend/SDL2Audio.hpp"
#include "../frontend/SDL2Input.hpp"
#endif

#include <iomanip>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

class TeeBuf final : public std::streambuf {
public:
    TeeBuf(std::streambuf* left, std::streambuf* right)
        : m_left(left), m_right(right) {}

protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        const int l = m_left ? m_left->sputc(static_cast<char>(c)) : c;
        const int r = m_right ? m_right->sputc(static_cast<char>(c)) : c;
        return (l == EOF || r == EOF) ? EOF : c;
    }

    int sync() override {
        const int l = m_left ? m_left->pubsync() : 0;
        const int r = m_right ? m_right->pubsync() : 0;
        return (l == 0 && r == 0) ? 0 : -1;
    }

private:
    std::streambuf* m_left;
    std::streambuf* m_right;
};

class TeeStream final : public std::ostream {
public:
    TeeStream(std::ostream& outA, std::ostream& outB)
        : std::ostream(nullptr), m_buf(outA.rdbuf(), outB.rdbuf()) {
        rdbuf(&m_buf);
    }

private:
    TeeBuf m_buf;
};

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void printRegs(const GB::RegisterFile& r, std::ostream& out) {
    out << std::hex << std::setfill('0')
        << "A:" << std::setw(2) << static_cast<int>(r.A) << " "
        << "F:" << std::setw(2) << static_cast<int>(r.F) << " "
        << "B:" << std::setw(2) << static_cast<int>(r.B) << " "
        << "C:" << std::setw(2) << static_cast<int>(r.C) << " "
        << "D:" << std::setw(2) << static_cast<int>(r.D) << " "
        << "E:" << std::setw(2) << static_cast<int>(r.E) << " "
        << "H:" << std::setw(2) << static_cast<int>(r.H) << " "
        << "L:" << std::setw(2) << static_cast<int>(r.L) << " "
        << "SP:" << std::setw(4) << r.SP << " "
        << "PC:" << std::setw(4) << r.PC
        << std::dec << "\n";
}

void printDebuggerHelp(std::ostream& out) {
    out << "help\n";
    out << "regs\n";
    out << "step [n]\n";
    out << "run [n]\n";
    out << "break <hexAddr>\n";
    out << "delbreak <hexAddr>\n";
    out << "mem <hexAddr> [len]\n";
    out << "write <hexAddr> <hexByte>\n";
    out << "include <scriptPath> (script mode)\n";
    out << "quit\n";
}

bool parseHexU16(const std::string& in, GB::u16& out) {
    std::stringstream ss;
    ss << std::hex << in;
    unsigned int v = 0;
    ss >> v;
    if (ss.fail() || v > 0xFFFFu) return false;
    out = static_cast<GB::u16>(v);
    return true;
}

bool parseHexU8(const std::string& in, GB::u8& out) {
    std::stringstream ss;
    ss << std::hex << in;
    unsigned int v = 0;
    ss >> v;
    if (ss.fail() || v > 0xFFu) return false;
    out = static_cast<GB::u8>(v);
    return true;
}

bool executeDebugCommand(const std::string& line, GB::GameBoy& gb, GB::Debugger& dbg, std::ostream& out) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty() || cmd[0] == '#') return true;

    if (cmd == "help") {
        printDebuggerHelp(out);
        return true;
    }

    if (cmd == "quit" || cmd == "q" || cmd == "exit") {
        return false;
    }

    if (cmd == "regs") {
        printRegs(dbg.registers(), out);
        return true;
    }

    if (cmd == "step") {
        int n = 1;
        iss >> n;
        if (n < 1) n = 1;
        for (int i = 0; i < n; ++i) {
            gb.stepInstruction();
        }
        out << "[debug] PC=0x" << std::hex << std::setw(4) << std::setfill('0')
            << dbg.programCounter() << std::dec << "\n";
        return true;
    }

    if (cmd == "run" || cmd == "continue") {
        unsigned long long limit = 1000000ULL;
        iss >> limit;
        const bool hit = dbg.runUntilBreakpoint(limit);
        if (hit) {
            out << "[debug] Breakpoint hit at PC=0x"
                << std::hex << std::setw(4) << std::setfill('0')
                << dbg.programCounter() << std::dec << "\n";
        } else {
            out << "[debug] Run limit reached; no breakpoint hit\n";
        }
        return true;
    }

    if (cmd == "break") {
        std::string addrStr;
        iss >> addrStr;
        GB::u16 addr = 0;
        if (!parseHexU16(addrStr, addr)) {
            out << "[debug] invalid address\n";
            return true;
        }
        dbg.addBreakpoint(addr);
        out << "[debug] breakpoint added at 0x" << std::hex << std::setw(4)
            << std::setfill('0') << addr << std::dec << "\n";
        return true;
    }

    if (cmd == "delbreak") {
        std::string addrStr;
        iss >> addrStr;
        GB::u16 addr = 0;
        if (!parseHexU16(addrStr, addr)) {
            out << "[debug] invalid address\n";
            return true;
        }
        dbg.removeBreakpoint(addr);
        out << "[debug] breakpoint removed at 0x" << std::hex << std::setw(4)
            << std::setfill('0') << addr << std::dec << "\n";
        return true;
    }

    if (cmd == "mem") {
        std::string addrStr;
        iss >> addrStr;
        int len = 16;
        iss >> len;
        if (len < 1) len = 1;
        if (len > 64) len = 64;

        GB::u16 addr = 0;
        if (!parseHexU16(addrStr, addr)) {
            out << "[debug] invalid address\n";
            return true;
        }

        out << std::hex << std::setfill('0');
        for (int i = 0; i < len; ++i) {
            GB::u16 a = static_cast<GB::u16>(addr + i);
            if ((i % 16) == 0) {
                out << "\n0x" << std::setw(4) << a << ": ";
            }
            out << std::setw(2) << static_cast<int>(dbg.readMemory(a)) << " ";
        }
        out << std::dec << "\n";
        return true;
    }

    if (cmd == "write") {
        std::string addrStr;
        std::string valueStr;
        iss >> addrStr >> valueStr;
        GB::u16 addr = 0;
        GB::u8 value = 0;
        if (!parseHexU16(addrStr, addr) || !parseHexU8(valueStr, value)) {
            out << "[debug] invalid write arguments\n";
            return true;
        }
        dbg.writeMemory(addr, value);
        out << "[debug] wrote 0x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(value) << " to 0x" << std::setw(4) << addr
            << std::dec << "\n";
        return true;
    }

    out << "[debug] unknown command: " << cmd << "\n";
    return true;
}

bool runDebuggerScript(const std::string& scriptPath,
                       GB::GameBoy& gb,
                       GB::Debugger& dbg,
                       std::ostream& out,
                       std::unordered_set<std::string>& includeStack) {
    namespace fs = std::filesystem;

    const fs::path rawPath(scriptPath);
    fs::path absPath;
    try {
        absPath = fs::absolute(rawPath);
    } catch (...) {
        out << "[debug] Failed to resolve script path: " << scriptPath << "\n";
        return false;
    }

    const std::string key = absPath.lexically_normal().string();
    if (includeStack.count(key) != 0) {
        out << "[debug] Include cycle detected for script: " << key << "\n";
        return false;
    }

    includeStack.insert(key);

    std::ifstream in(absPath.string());
    if (!in) {
        out << "[debug] Failed to open script: " << absPath.string() << "\n";
        includeStack.erase(key);
        return false;
    }

    out << "[debug] Running script: " << absPath.string() << "\n";
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        out << "[debug][script:" << lineNo << "] " << line << "\n";

        const std::string t = trim(line);
        if (!t.empty() && t[0] != '#') {
            std::istringstream cmdIss(t);
            std::string cmd;
            cmdIss >> cmd;
            if (cmd == "include") {
                std::string includeArg;
                cmdIss >> includeArg;
                if (includeArg.empty()) {
                    out << "[debug] include requires a path argument\n";
                    includeStack.erase(key);
                    return false;
                }

                fs::path includePath(includeArg);
                if (includePath.is_relative()) {
                    includePath = absPath.parent_path() / includePath;
                }

                if (!runDebuggerScript(includePath.string(), gb, dbg, out, includeStack)) {
                    includeStack.erase(key);
                    return false;
                }
                continue;
            }
        }

        if (!executeDebugCommand(line, gb, dbg, out)) {
            out << "[debug] Script requested quit\n";
            includeStack.erase(key);
            return false;
        }
    }

    includeStack.erase(key);
    return true;
}

void debuggerLoop(GB::GameBoy& gb, const std::string& scriptPath, std::ostream& out) {
    GB::Debugger dbg(gb);

    out << "[debug] Interactive debugger mode\n";
    out << "[debug] Commands: help, regs, step [n], run [n], break <addr>, delbreak <addr>, mem <addr> [len], write <addr> <byte>, include <path>, quit\n";

    if (!scriptPath.empty()) {
        std::unordered_set<std::string> includeStack;
        const bool continueSession = runDebuggerScript(scriptPath, gb, dbg, out, includeStack);
        if (!continueSession) {
            return;
        }
    }

    std::string line;
    while (true) {
        out << "dbg> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (!executeDebugCommand(line, gb, dbg, out)) {
            break;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom.gb> [save.sav] [--load-state path] [--save-state path] [--debugger] [--debug-script path] [--debug-log path]\n";
        return 1;
    }

    std::string romPath  = argv[1];
    std::string savePath = (argc >= 3) ? argv[2] : "";
    std::string loadStatePath;
    std::string saveStatePath;
    std::string debugScriptPath;
    std::string debugLogPath;
    bool debuggerMode = false;

    // Treat the first non-option argument after ROM path as optional save path.
    savePath.clear();
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--debugger") {
            debuggerMode = true;
            continue;
        }

        if (arg == "--load-state" || arg == "--save-state" ||
            arg == "--debug-script" || arg == "--debug-log") {
            if (i + 1 >= argc) {
                std::cerr << "[main] Missing value for option: " << arg << "\n";
                return 1;
            }

            const std::string value = argv[++i];
            if (arg == "--load-state") {
                loadStatePath = value;
            } else if (arg == "--save-state") {
                saveStatePath = value;
            } else if (arg == "--debug-script") {
                debugScriptPath = value;
            } else if (arg == "--debug-log") {
                debugLogPath = value;
            }
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "[main] Unknown option: " << arg << "\n";
            return 1;
        }

        if (savePath.empty()) {
            savePath = arg;
        } else {
            std::cerr << "[main] Unexpected positional argument: " << arg << "\n";
            return 1;
        }
    }

    if (!debugScriptPath.empty()) {
        debuggerMode = true;
    }

    try {
#ifdef GB_SDL2_BACKEND
        GB::SDL2Display display;
        GB::SDL2Audio audio;
        GB::GameBoy gb(&display, [&audio](float left, float right) {
            audio.pushSample(left, right);
        });
#else
        GB::GameBoy gb(nullptr, nullptr);
#endif

        if (!gb.loadROM(romPath, savePath)) {
            std::cerr << "[main] Failed to load ROM: " << romPath << '\n';
            return 1;
        }

        if (!loadStatePath.empty()) {
            std::string loadErr;
            if (!GB::SaveState::loadFromFile(gb, loadStatePath, &loadErr)) {
                std::cerr << "[main] Failed to load state: " << loadStatePath
                          << " (" << loadErr << ")\n";
            }
        }

        std::cout << "[main] ROM loaded. Starting emulation...\n";
        std::cout << "[main] Cartridge: " << gb.cartridge().header().title << '\n';

        if (debuggerMode) {
            std::ofstream debugLog;
            std::unique_ptr<TeeStream> tee;
            std::ostream* debugOut = &std::cout;

            if (!debugLogPath.empty()) {
                debugLog.open(debugLogPath, std::ios::out | std::ios::trunc);
                if (!debugLog) {
                    std::cerr << "[main] Failed to open debug log: " << debugLogPath << "\n";
                } else {
                    tee = std::make_unique<TeeStream>(std::cout, debugLog);
                    debugOut = tee.get();
                    (*debugOut) << "[debug] Logging to: " << debugLogPath << "\n";
                }
            }

            debuggerLoop(gb, debugScriptPath, *debugOut);
        }

#ifdef GB_SDL2_BACKEND
        if (!debuggerMode) {
            while (display.isOpen()) {
                if (!GB::SDL2Input::pollAndDispatch(gb)) {
                    break;
                }
                gb.runFrame();
            }
        }
#else
        if (!debuggerMode) {
            for (int frame = 0; frame < 60; ++frame) {
                gb.runFrame();
            }
        }
#endif

        std::cout << "[main] Session complete. CPU cycles: "
                  << gb.cpu().getCycleCount() << '\n';

        // ── Persist SRAM before exit ──────────────────────────────────────────
        if (!savePath.empty()) {
            gb.saveSRAM(savePath);
            std::cout << "[main] SRAM saved to: " << savePath << '\n';
        }

        if (!saveStatePath.empty()) {
            std::string saveErr;
            if (GB::SaveState::saveToFile(gb, saveStatePath, &saveErr)) {
                std::cout << "[main] Save-state written: " << saveStatePath << '\n';
            } else {
                std::cerr << "[main] Failed to write save-state: " << saveStatePath
                          << " (" << saveErr << ")\n";
            }
        }

    } catch (const std::exception& ex) {
        std::cerr << "[main] Fatal: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
