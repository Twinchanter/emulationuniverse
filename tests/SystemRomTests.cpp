#include "GameBoy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct RomCase {
    std::string suite;
    std::string path;
    std::string mode; // blargg or mooneye
    std::string priority; // critical, high, medium, low
    int maxFrames = 0;
};

struct Result {
    RomCase test;
    bool passed = false;
    std::string detail;
};

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string escapeCsv(std::string value) {
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');

    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '"') {
            escaped += '"';
            escaped += '"';
        } else {
            escaped += ch;
        }
    }
    return '"' + escaped + '"';
}

int defaultMaxFramesForMode(const std::string& modeRaw) {
    const std::string mode = toLower(modeRaw);
    if (mode == "blargg") return 1200;
    if (mode == "mooneye") return 1800;
    return 1800;
}

int parsePositiveIntOrDefault(const std::string& raw, int fallback) {
    if (raw.empty()) return fallback;
    try {
        const int value = std::stoi(raw);
        if (value > 0) return value;
    } catch (...) {
        return fallback;
    }
    return fallback;
}

std::vector<RomCase> loadManifest(const fs::path& manifestPath) {
    std::vector<RomCase> out;

    std::ifstream in(manifestPath);
    if (!in) {
        return out;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Format: suite,mode,path[,priority[,max_frames]]
        // Supports quoted fields for paths containing commas (e.g. "03-op sp,hl.gb")
        std::vector<std::string> fields;
        {
            bool inQuote = false;
            std::string cur;
            for (char ch : line) {
                if (ch == '"') {
                    inQuote = !inQuote;
                } else if (ch == ',' && !inQuote) {
                    fields.push_back(trim(cur));
                    cur.clear();
                } else {
                    cur += ch;
                }
            }
            fields.push_back(trim(cur));
        }

        if (fields.size() < 3) continue;
        if (toLower(fields[0]) == "suite" && toLower(fields[1]) == "mode") {
            continue; // header row
        }

        std::string suite = fields[0];
        std::string mode  = fields[1];
        std::string path  = fields[2];
        std::string priority = (fields.size() >= 4 && !fields[3].empty()) ? toLower(fields[3]) : "high";
        int maxFrames = (fields.size() >= 5)
            ? parsePositiveIntOrDefault(fields[4], defaultMaxFramesForMode(mode))
            : defaultMaxFramesForMode(mode);

        if (suite.empty() || mode.empty() || path.empty()) continue;

        if (priority != "critical" && priority != "high" && priority != "medium" && priority != "low") {
            priority = "high";
        }

        out.push_back({suite, path, mode, priority, maxFrames});
    }

    return out;
}

bool runBlarggCase(GB::GameBoy& gb, Result& res, int maxFrames) {
    // 0xFF means RAM not yet enabled / uninitialized – treat as "not ready".
    // 0x80 is the blargg "still running" sentinel.
    // Any other non-zero value after the ROM has signalled 0x80 is a failure code.
    bool seenRunning = false;
    gb.clearSerial();

    for (int i = 0; i < maxFrames; ++i) {
        gb.runFrame();
        const GB::u8 status = gb.readByte(0xA000);
        const std::string& serial = gb.serialOutput();

        // Some blargg ROMs (notably MBC1/no-RAM cpu_instrs) report result via serial only.
        if (serial.find("Passed") != std::string::npos || serial.find("Passed all tests") != std::string::npos) {
            res.passed = true;
            res.detail = "blargg serial PASS";
            return true;
        }
        if (serial.find("Failed") != std::string::npos) {
            res.passed = false;
            const size_t keep = 120;
            const std::string tail = (serial.size() > keep) ? serial.substr(serial.size() - keep) : serial;
            res.detail = "blargg serial FAIL: " + tail;
            return false;
        }

        if (status == 0xFF) {
            continue; // RAM not yet enabled or not initialized – keep waiting
        }
        if (status == 0x80) {
            seenRunning = true;
            continue; // test is running
        }
        if (status == 0x00) {
            res.passed = true;
            res.detail = "blargg status 0x00";
            return true;
        }
        // Non-zero, non-0x80 status after ROM has been running = failure code
        if (seenRunning) {
            res.passed = false;
            res.detail = "blargg status=0x" + std::to_string(status);
            return false;
        }
        // Failure code before 0x80 seen may be stale RAM; keep waiting
    }

    res.detail = "timeout waiting for blargg status (max_frames=" + std::to_string(maxFrames) + ")";
    return false;
}

bool runMooneyeCase(GB::GameBoy& gb, Result& res, int maxFrames) {
    gb.clearSerial();

    for (int i = 0; i < maxFrames; ++i) {
        gb.runFrame();
        const GB::RegisterFile& r = gb.cpu().registers();
        const std::string& serial = gb.serialOutput();

        // Mooneye pass: B,C,D,E,H,L = 3,5,8,13,21,34
        if (r.B == 3 && r.C == 5 && r.D == 8 && r.E == 13 && r.H == 21 && r.L == 34) {
            res.passed = true;
            res.detail = "mooneye fibonacci register signature";
            return true;
        }
        // Mooneye fail: B,C,D,E,H,L = 0x42,0x42,0x42,0x42,0x42,0x42
        if (r.B == 0x42 && r.C == 0x42 && r.D == 0x42 && r.E == 0x42 && r.H == 0x42 && r.L == 0x42) {
            res.passed = false;
            if (!serial.empty()) {
                constexpr size_t maxKeep = 400;
                std::string excerpt;
                if (serial.size() <= maxKeep) {
                    excerpt = serial;
                } else {
                    constexpr size_t halfKeep = maxKeep / 2;
                    excerpt = serial.substr(0, halfKeep)
                            + " ... "
                            + serial.substr(serial.size() - halfKeep);
                }
                res.detail = "mooneye FAIL signature (B=C=D=E=H=L=0x42): " + excerpt;
            } else {
                res.detail = "mooneye FAIL signature (B=C=D=E=H=L=0x42)";
            }
            return false;
        }
    }

    const std::string& serial = gb.serialOutput();
    if (!serial.empty()) {
        const size_t keep = 160;
        const std::string tail = (serial.size() > keep) ? serial.substr(serial.size() - keep) : serial;
        res.detail = "timeout waiting for mooneye signature (max_frames=" + std::to_string(maxFrames) + "): " + tail;
    } else {
        res.detail = "timeout waiting for mooneye signature (max_frames=" + std::to_string(maxFrames) + ")";
    }
    return false;
}

void writeCsvLog(const fs::path& logPath, const std::vector<Result>& results) {
    fs::create_directories(logPath.parent_path());

    std::ofstream out(logPath, std::ios::trunc);
    out << "suite,priority,max_frames,rom,passed,detail\n";
    for (const Result& r : results) {
        out << r.test.suite << ','
            << r.test.priority << ','
            << r.test.maxFrames << ','
            << escapeCsv(r.test.path) << ','
            << (r.passed ? "PASS" : "FAIL") << ','
            << escapeCsv(r.detail)
            << "\n";
    }
}

} // namespace

int main() {
    std::string root;
#if defined(_MSC_VER)
    char* rootEnv = nullptr;
    size_t len = 0;
    if (_dupenv_s(&rootEnv, &len, "EMU_TEST_ROMS_ROOT") == 0 && rootEnv) {
        root.assign(rootEnv);
        free(rootEnv);
    }
#else
    const char* rootEnv = std::getenv("EMU_TEST_ROMS_ROOT");
    if (rootEnv) {
        root.assign(rootEnv);
    }
#endif

    if (root.empty()) {
        // Auto-detect common workspace layouts when env var is absent.
        if (fs::exists("roms/test")) {
            root = "roms/test";
        } else if (fs::exists("../roms/test")) {
            root = "../roms/test";
        }
    }

    if (root.empty()) {
        std::cerr << "[tests] EMU_TEST_ROMS_ROOT is not set and roms/test was not found; skipping ROM suite run.\n";
        return 0;
    }

    std::string manifest = "tests/rom_manifest.csv";
#if defined(_MSC_VER)
    char* manifestEnv = nullptr;
    size_t manifestLen = 0;
    if (_dupenv_s(&manifestEnv, &manifestLen, "EMU_TEST_MANIFEST") == 0 && manifestEnv) {
        manifest.assign(manifestEnv);
        free(manifestEnv);
    }
#else
    const char* manifestEnv = std::getenv("EMU_TEST_MANIFEST");
    if (manifestEnv) {
        manifest.assign(manifestEnv);
    }
#endif

    const fs::path romRoot(root);
    const std::vector<RomCase> cases = loadManifest(manifest);
    if (cases.empty()) {
        std::cerr << "[tests] ROM manifest empty or missing: " << manifest << "\n";
        return 1;
    }

    std::vector<Result> results;
    results.reserve(cases.size());

    for (const RomCase& tc : cases) {
        Result res{};
        res.test = tc;

        const fs::path romPath = romRoot / tc.path;
        if (!fs::exists(romPath)) {
            res.passed = false;
            res.detail = "missing ROM file";
            results.push_back(res);
            continue;
        }

        try {
            GB::GameBoy gb(nullptr, nullptr);
            gb.loadROM(romPath.string());

            if (toLower(tc.mode) == "blargg") {
                runBlarggCase(gb, res, tc.maxFrames);
            } else {
                runMooneyeCase(gb, res, tc.maxFrames);
            }
        } catch (const std::exception& ex) {
            res.passed = false;
            res.detail = std::string("exception: ") + ex.what();
        }

        std::cout << "[tests] [" << tc.priority << "] " << tc.suite << " :: " << tc.path
                  << " => " << (res.passed ? "PASS" : "FAIL")
                  << " (" << res.detail << ")\n";
        results.push_back(res);
    }

    const fs::path logPath = fs::path("tests") / "logs" / "rom_suite_results.csv";
    writeCsvLog(logPath, results);

    bool allPassed = true;
    for (const Result& r : results) {
        allPassed = allPassed && r.passed;
    }

    std::cout << "[tests] Wrote log: " << logPath.string() << "\n";
    return allPassed ? 0 : 1;
}
