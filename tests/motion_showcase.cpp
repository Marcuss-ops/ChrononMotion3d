#include "motion_showcase_internal.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
using namespace motion_showcase_detail;
namespace {
std::map<std::string, std::string> readGoldenManifest() {
#ifndef CHRONONMOTION_SHOWCASE_GOLDEN
#error "CHRONONMOTION_SHOWCASE_GOLDEN must point to the committed golden manifest"
#endif
    std::ifstream input(CHRONONMOTION_SHOWCASE_GOLDEN, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot read golden manifest: ") + CHRONONMOTION_SHOWCASE_GOLDEN);
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::map<std::string, std::string> result;
    const std::regex item(R"golden("([a-z0-9_]+)"\s*:\s*"([0-9a-f]{16})")golden");
    for (std::sregex_iterator it(contents.begin(), contents.end(), item), end; it != end; ++it)
        result.emplace((*it)[1].str(), (*it)[2].str());
    return result;
}

void printGoldenManifest(const std::map<std::string, std::string>& goldens) {
    std::cout << "{\n  \"version\": 1,\n  \"hash\": \"FNV-1a-64 over length-delimited scene JSON and sampled output\",\n  \"cases\": {\n";
    std::size_t index = 0;
    for (const auto& [id, value] : goldens) {
        std::cout << "    \"" << id << "\": \"" << value << "\"";
        if (++index != goldens.size()) std::cout << ',';
        std::cout << '\n';
    }
    std::cout << "  }\n}\n";
}

int runOne(const ShowcaseCase& showcase, const std::map<std::string, std::string>& goldens,
           bool dumpGolden) {
    if (!showcase.skippedBecause.empty()) {
        std::cout << "[SKIP] " << std::setw(2) << std::setfill('0') << showcase.number << ' '
                  << showcase.id << " — " << showcase.skippedBecause << '\n';
        return 2;
    }
    Checks checks;
    Snapshot snapshot;
    try {
        showcase.run(checks, snapshot);
    } catch (const std::exception& error) {
        std::cout << "[FAIL] " << showcase.id << " — exception: " << error.what() << '\n';
        return 1;
    }
    const std::string signature = hash64(snapshot.data());
    if (!dumpGolden) {
        const auto expected = goldens.find(showcase.id);
        if (expected == goldens.end()) {
            std::cout << "[FAIL] " << showcase.id << " — missing golden signature\n";
            ++checks.failures;
        } else if (expected->second != signature) {
            std::cout << "[FAIL] " << showcase.id << " — golden mismatch (actual " << signature
                      << ", expected " << expected->second << ")\n";
            ++checks.failures;
        }
    }
    if (dumpGolden) std::cout << "  \"" << showcase.id << "\": \"" << signature << "\"\n";
    if (checks.failures == 0) {
        std::cout << "[PASS] " << std::setw(2) << std::setfill('0') << showcase.number << ' '
                  << showcase.id << " (" << checks.count << " assertions)\n";
        return 0;
    }
    std::cout << "[FAIL] " << showcase.id << " (" << checks.failures << '/' << checks.count
              << " assertions failed)\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    const auto cases = makeCases();
    bool all = false;
    bool dumpGolden = false;
    std::string selected;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--all") all = true;
        else if (argument == "--dump-golden") dumpGolden = true;
        else if (argument == "--list") list = true;
        else if (argument == "--case" && i + 1 < argc) selected = argv[++i];
        else {
            std::cerr << "usage: chrononmotion-showcase (--all | --case ID | --list | --dump-golden)\n";
            return 2;
        }
    }
    if (static_cast<int>(all) + static_cast<int>(!selected.empty()) + static_cast<int>(list) +
        static_cast<int>(dumpGolden) != 1) {
        std::cerr << "choose exactly one of --all, --case ID, --list, --dump-golden\n";
        return 2;
    }
    if (list) {
        for (const auto& showcase : cases) {
            std::cout << std::setw(2) << std::setfill('0') << showcase.number << ' ' << showcase.id;
            if (!showcase.skippedBecause.empty()) std::cout << " [SKIP: " << showcase.skippedBecause << ']';
            std::cout << " — " << showcase.description << '\n';
        }
        return 0;
    }

    try {
        const auto goldens = dumpGolden ? std::map<std::string, std::string>{} : readGoldenManifest();
        if (!dumpGolden) {
            std::size_t supported = 0;
            for (const auto& showcase : cases) {
                if (!showcase.skippedBecause.empty()) continue;
                ++supported;
                if (!goldens.contains(showcase.id))
                    throw std::runtime_error("golden manifest is missing supported case " + showcase.id);
            }
            std::size_t expectedGoldens = supported;
            for (const auto& showcase : cases) {
                if (showcase.number == 59 && !showcase.skippedBecause.empty()) ++expectedGoldens;
            }
            if (goldens.size() != expectedGoldens)
                throw std::runtime_error("golden manifest has entries that are not acceptance cases for this configuration");
            for (const auto& [id, signature] : goldens) {
                (void)signature;
                const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto& showcase) {
                    return showcase.id == id;
                });
                if (found == cases.end() ||
                    (!found->skippedBecause.empty() && found->number != 59))
                    throw std::runtime_error("golden manifest has an entry for a non-executable acceptance case " + id);
            }
        }
        if (dumpGolden) {
            std::map<std::string, std::string> generated;
            for (const auto& showcase : cases) {
                if (!showcase.skippedBecause.empty()) continue;
                Checks checks; Snapshot snapshot;
                showcase.run(checks, snapshot);
                if (checks.failures != 0) {
                    std::cerr << "cannot emit goldens while assertions fail in " << showcase.id << '\n';
                    return 1;
                }
                generated.emplace(showcase.id, hash64(snapshot.data()));
            }
#if !defined(CHRONONMOTION_SHOWCASE_HAS_ADAPTER)
            const auto existingGoldens = readGoldenManifest();
            const auto adapterGolden = existingGoldens.find("renderplan_roundtrip");
            if (adapterGolden != existingGoldens.end())
                generated.emplace(adapterGolden->first, adapterGolden->second);
#endif
            printGoldenManifest(generated);
            return 0;
        }

        if (!selected.empty()) {
            const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto& item) {
                return item.id == selected || std::to_string(item.number) == selected;
            });
            if (found == cases.end()) {
                std::cerr << "unknown showcase case: " << selected << '\n';
                return 2;
            }
            const int result = runOne(*found, goldens, false);
            return result == 2 ? 0 : result;
        }

        unsigned passed = 0, skipped = 0, failed = 0, assertionCount = 0;
        for (const auto& showcase : cases) {
            if (!showcase.skippedBecause.empty()) {
                ++skipped;
                (void)runOne(showcase, goldens, false);
                continue;
            }
            Checks checks; Snapshot snapshot;
            try {
                showcase.run(checks, snapshot);
            } catch (const std::exception& error) {
                ++failed;
                std::cout << "[FAIL] " << showcase.id << " — exception: " << error.what() << '\n';
                continue;
            }
            assertionCount += static_cast<unsigned>(checks.count);
            const std::string signature = hash64(snapshot.data());
            const auto expected = goldens.find(showcase.id);
            if (checks.failures != 0 || expected == goldens.end() || expected->second != signature) {
                ++failed;
                if (expected == goldens.end())
                    std::cout << "[FAIL] " << showcase.id << " — missing golden signature\n";
                else if (expected->second != signature)
                    std::cout << "[FAIL] " << showcase.id << " — golden mismatch (actual " << signature
                              << ", expected " << expected->second << ")\n";
                else
                    std::cout << "[FAIL] " << showcase.id << " (" << checks.failures << '/' << checks.count
                              << " assertions failed)\n";
                continue;
            }
            ++passed;
            std::cout << "[PASS] " << std::setw(2) << std::setfill('0') << showcase.number << ' '
                      << showcase.id << " (" << checks.count << " assertions)\n";
        }
        std::cout << "\nChrononMotion Acceptance Suite\nPASS " << passed << '/' << (cases.size() - skipped)
                  << "; SKIP " << skipped << '/' << cases.size() << "; FAIL " << failed
                  << "; ASSERTIONS " << assertionCount << '\n';
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "chrononmotion-showcase: " << error.what() << '\n';
        return 2;
    }
}
