// PDF watermark detection / removal (vector watermarks only). Depends on QPDF, not on Qt.
// Scope of this version: (1) stand-alone "artifact" objects (a Form XObject stamped on every page),
// (2) flattened diagonal text watermarks whose overlap with pattern lines was baked into mixed colours.
// Same-layer shop blocks and bitmap pages are reported but never touched.
#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace wm {

enum class Kind { ArtifactObject, FlatText, PageStamp, Bitmap };
enum class Tier { High, Confirm, Keep, Unsupported };   // High = removed by default

struct Finding {
    Kind kind{};
    Tier tier = Tier::Keep;
    int score = 0;                 // 0..100
    std::string label;             // UTF-8, for the UI / report
    int pages = 0;                 // pages on which it occurs
    int objects = 0;               // drawing objects (form instances / text blocks)
    bool contactText = false;      // watermark text looks like contact info (reported as a flag only, never the text itself)
    std::vector<std::string> evidence;   // "reason +points" lines, UTF-8
    // internal plan data
    std::string formKey;           // ArtifactObject: group key
    double alpha = 0;              // FlatText: fitted mixing ratio
    std::vector<double> lightColour;                       // FlatText: colour of the watermark glyphs over white
    std::map<std::string, std::vector<double>> restore;    // FlatText: blended colour key -> original colour
};

struct Report {
    int pageCount = 0;
    bool ok = false;
    std::string error;
    std::vector<Finding> findings;
    bool hasRemovable() const;
};

struct RemoveResult {
    bool ok = false;
    std::string error;
    int formsRemoved = 0, textBlocksRemoved = 0, fillsRemoved = 0, coloursRestored = 0;
};

Report analyze(const std::filesystem::path& in);
// Removes every finding with tier High, plus the ones whose index is listed in `alsoRemove` (user confirmed).
RemoveResult remove(const std::filesystem::path& in, const std::filesystem::path& out,
                    const std::vector<int>& alsoRemove = {});

std::string toJson(const Report&);

}  // namespace wm
