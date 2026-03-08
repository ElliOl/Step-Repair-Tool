/**
 * Text-level STEP file patcher — no OpenCASCADE geometry round-trip.
 *
 * Parses the DATA section into a flat entity list, builds the minimum set of
 * lookup maps needed for name repair and HOOPS compat, then writes the output
 * file by copying the original and applying targeted substitutions.
 */

#include "step_text_patch.h"
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <iterator>

namespace StepFixerNative {

// ---------------------------------------------------------------------------
// Internal entity representation
// ---------------------------------------------------------------------------

struct StepEntity {
    int         id        = 0;
    std::string type;    // primary type name, e.g. "PRODUCT"
    std::string params;  // raw text inside the outermost () of the entity
    size_t      byteStart = 0; // offset of '#' in the file content
    size_t      byteEnd   = 0; // offset after the trailing newline
};

// ---------------------------------------------------------------------------
// Entity collector
// ---------------------------------------------------------------------------

static std::vector<StepEntity> CollectEntities(const std::string& content) {
    std::vector<StepEntity> entities;

    // Locate DATA section
    size_t pos = content.find("\nDATA;");
    if (pos == std::string::npos) pos = content.find("DATA;");
    if (pos == std::string::npos) return entities;
    pos = content.find(';', pos) + 1;

    size_t dataEnd = content.find("\nENDSEC;", pos);
    if (dataEnd == std::string::npos) dataEnd = content.size();

    while (pos < dataEnd) {
        // Entity definitions start with #NNN=
        if (content[pos] != '#') { ++pos; continue; }

        size_t numStart = pos + 1;
        size_t numEnd   = numStart;
        while (numEnd < dataEnd && isdigit((unsigned char)content[numEnd])) ++numEnd;
        if (numEnd == numStart || numEnd >= dataEnd || content[numEnd] != '=') {
            ++pos; continue;
        }

        StepEntity ent;
        ent.id        = std::stoi(content.substr(numStart, numEnd - numStart));
        ent.byteStart = pos;

        size_t cur = numEnd + 1; // after '='

        // Parse type name (letters/digits/underscores up to '(')
        if (cur < dataEnd && content[cur] != '(') {
            size_t typeStart = cur;
            while (cur < dataEnd && content[cur] != '(' && content[cur] != ';' && content[cur] != '\n')
                ++cur;
            ent.type = content.substr(typeStart, cur - typeStart);
        }

        // Advance to opening '(' of the entity's parameter list
        while (cur < dataEnd && content[cur] != '(') ++cur;
        if (cur >= dataEnd) { pos = cur; continue; }
        ++cur; // consume '('

        // Collect the params up to the matching ')'
        size_t paramsStart = cur;
        int    depth       = 1;
        bool   inStr       = false;
        while (cur < dataEnd && depth > 0) {
            char c = content[cur];
            if (inStr) {
                if (c == '\'') {
                    // '' = escaped single-quote in STEP strings
                    if (cur + 1 < dataEnd && content[cur + 1] == '\'') ++cur;
                    else inStr = false;
                }
            } else {
                if      (c == '\'') inStr = true;
                else if (c == '(')  ++depth;
                else if (c == ')') { --depth; if (depth == 0) break; }
            }
            ++cur;
        }
        ent.params = content.substr(paramsStart, cur - paramsStart);
        ++cur; // consume closing ')'

        // Skip to ';'
        while (cur < dataEnd && content[cur] != ';') ++cur;
        if (cur < dataEnd) ++cur; // consume ';'
        // Include trailing CR/LF so the patch range covers the full line
        if (cur < content.size() && content[cur] == '\r') ++cur;
        if (cur < content.size() && content[cur] == '\n') ++cur;

        ent.byteEnd = cur;
        entities.push_back(std::move(ent));
        pos = ent.byteEnd;
    }
    return entities;
}

// ---------------------------------------------------------------------------
// Parameter helpers
// ---------------------------------------------------------------------------

// Return the Nth top-level parameter (0-indexed) from a STEP params string.
// Handles nested parens, quoted strings with '' escaping, and whitespace.
static std::string GetNthParam(const std::string& params, int n) {
    int    depth  = 0;
    int    count  = 0;
    bool   inStr  = false;
    size_t start  = 0;

    // Skip leading whitespace for the first param
    while (start < params.size() &&
           (params[start] == ' ' || params[start] == '\t' ||
            params[start] == '\r'|| params[start] == '\n'))
        ++start;

    for (size_t i = start; i < params.size(); ++i) {
        char c = params[i];
        if (inStr) {
            if (c == '\'') {
                if (i + 1 < params.size() && params[i + 1] == '\'') ++i;
                else inStr = false;
            }
            continue;
        }
        if      (c == '\'')              inStr = true;
        else if (c == '(' || c == '[')   ++depth;
        else if (c == ')' || c == ']')   --depth;
        else if (depth == 0 && c == ',') {
            if (count == n) {
                std::string r = params.substr(start, i - start);
                size_t rs = r.find_first_not_of(" \t\r\n");
                size_t re = r.find_last_not_of(" \t\r\n");
                return (rs == std::string::npos) ? "" : r.substr(rs, re - rs + 1);
            }
            ++count;
            start = i + 1;
            while (start < params.size() &&
                   (params[start] == ' '  || params[start] == '\t' ||
                    params[start] == '\r' || params[start] == '\n'))
                ++start;
            i = start - 1;
        }
    }
    if (count == n) {
        std::string r = params.substr(start);
        size_t rs = r.find_first_not_of(" \t\r\n");
        size_t re = r.find_last_not_of(" \t\r\n");
        return (rs == std::string::npos) ? "" : r.substr(rs, re - rs + 1);
    }
    return "";
}

// Return raw params text from index startN to end (after the Nth comma).
static std::string GetParamsFrom(const std::string& params, int startN) {
    int  depth = 0, count = 0;
    bool inStr = false;
    for (size_t i = 0; i < params.size(); ++i) {
        char c = params[i];
        if (inStr) {
            if (c == '\'') { if (i+1<params.size()&&params[i+1]=='\'') ++i; else inStr=false; }
            continue;
        }
        if      (c == '\'')            inStr = true;
        else if (c == '('||c == '[')   ++depth;
        else if (c == ')'||c == ']')   --depth;
        else if (depth == 0 && c == ',') {
            ++count;
            if (count == startN) return params.substr(i + 1);
        }
    }
    return "";
}

// Extract the string value from a STEP single-quoted param (handles '' escaping).
static std::string ExtractString(const std::string& param) {
    size_t s = param.find('\'');
    if (s == std::string::npos) return "";
    ++s;
    std::string result;
    for (size_t i = s; i < param.size(); ++i) {
        if (param[i] == '\'') {
            if (i + 1 < param.size() && param[i + 1] == '\'') { result += '\''; ++i; }
            else break;
        } else { result += param[i]; }
    }
    return result;
}

// Extract the entity reference number from a "#NNN" token.
static int ExtractRef(const std::string& param) {
    size_t pos = param.find('#');
    if (pos == std::string::npos) return -1;
    int id = 0;
    for (size_t i = pos + 1; i < param.size() && isdigit((unsigned char)param[i]); ++i)
        id = id * 10 + (param[i] - '0');
    return id;
}

// Collect all #NNN reference integers from a string (handles any surrounding text).
static std::vector<int> ParseRefList(const std::string& s) {
    std::vector<int> refs;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '#') continue;
        int id = 0;
        size_t j = i + 1;
        while (j < s.size() && isdigit((unsigned char)s[j])) { id = id*10+(s[j]-'0'); ++j; }
        if (j > i + 1) { refs.push_back(id); i = j - 1; }
    }
    return refs;
}

// Escape a name for use as a STEP string literal (' → '').
static std::string EscapeStepString(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) { if (c == '\'') r += "''"; else r += c; }
    return r;
}

// True if name looks like a file path (NAUO carries the embedding file's name).
static bool LooksLikeFilePath(const std::string& name) {
    if (name.size() < 5) return false;
    std::string lo = name;
    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
    return lo.size() > 4 &&
           (lo.substr(lo.size()-4) == ".stp"  ||
            lo.substr(lo.size()-5) == ".step" ||
            lo.substr(lo.size()-4) == ".p21"  ||
            lo.substr(lo.size()-5) == ".p21e");
}

// ---------------------------------------------------------------------------
// Core implementation — operates on already-loaded content.
// ---------------------------------------------------------------------------

static bool PatchContentImpl(
    const std::string& content,
    const std::string& outputPath,
    bool fixNames,
    bool fixHoopsCompat)
{
    std::vector<StepEntity> entities = CollectEntities(content);

    struct Patch { size_t start, end; std::string text; };
    std::vector<Patch> patches;

    // -----------------------------------------------------------------------
    // Fix 1: Name repair
    //
    // For each PRODUCT entity whose name field is '0', determine the real
    // part name by tracing:
    //   PRODUCT → PDForm → PD → PDS → SDR → SR → SRR → ABSR → MSB name
    // and falling back to the NAUO instance name when the MSB path fails.
    // -----------------------------------------------------------------------
    if (fixNames) {
        // --- MSB name path ---------------------------------------------------
        // MSB: MANIFOLD_SOLID_BREP('name', ...)  →  msb_id → name
        std::unordered_map<int,std::string> msbName;
        for (const auto& e : entities) {
            if (e.type != "MANIFOLD_SOLID_BREP") continue;
            std::string n = ExtractString(GetNthParam(e.params, 0));
            if (!n.empty() && n != "0" && n != " ") msbName[e.id] = n;
        }

        // ADVANCED_BREP_SHAPE_REPRESENTATION('', (items...), #ctx)
        // param 1 items tuple contains the MSB ref  →  absr_id → msb name
        std::unordered_map<int,std::string> absrName;
        for (const auto& e : entities) {
            if (e.type != "ADVANCED_BREP_SHAPE_REPRESENTATION") continue;
            for (int ref : ParseRefList(GetNthParam(e.params, 1))) {
                auto it = msbName.find(ref);
                if (it != msbName.end()) { absrName[e.id] = it->second; break; }
            }
        }

        // SHAPE_REPRESENTATION_RELATIONSHIP('','',#sr,#absr)
        // The SR and ABSR can appear in either order; identify which is ABSR.
        // → sr_id → msb name
        std::unordered_map<int,std::string> srName;
        for (const auto& e : entities) {
            if (e.type != "SHAPE_REPRESENTATION_RELATIONSHIP") continue;
            int id2 = ExtractRef(GetNthParam(e.params, 2));
            int id3 = ExtractRef(GetNthParam(e.params, 3));
            if (id2 < 0 || id3 < 0) continue;
            auto it3 = absrName.find(id3);
            auto it2 = absrName.find(id2);
            if      (it3 != absrName.end()) srName[id2] = it3->second;
            else if (it2 != absrName.end()) srName[id3] = it2->second;
        }

        // SHAPE_DEFINITION_REPRESENTATION(#pds, #sr)
        // → pds_id → msb name
        std::unordered_map<int,std::string> pdsName;
        for (const auto& e : entities) {
            if (e.type != "SHAPE_DEFINITION_REPRESENTATION") continue;
            int pds_id = ExtractRef(GetNthParam(e.params, 0));
            int sr_id  = ExtractRef(GetNthParam(e.params, 1));
            auto it = srName.find(sr_id);
            if (it != srName.end() && pds_id >= 0) pdsName[pds_id] = it->second;
        }

        // PRODUCT_DEFINITION_SHAPE('','', #pd)
        // → pd_id → msb name
        std::unordered_map<int,std::string> pdMsbName;
        for (const auto& e : entities) {
            if (e.type != "PRODUCT_DEFINITION_SHAPE") continue;
            int pd_id = ExtractRef(GetNthParam(e.params, 2));
            auto it = pdsName.find(e.id);
            if (it != pdsName.end() && pd_id >= 0) pdMsbName[pd_id] = it->second;
        }

        // --- NAUO name path (fallback) ----------------------------------------
        // PRODUCT_DEFINITION_FORMATION[_WITH_SPECIFIED_SOURCE]('','',#product,...)
        // → product_id → pdform_id
        std::unordered_map<int,int> formByProduct;
        // PRODUCT_DEFINITION('','',#pdform,...)
        // → pdform_id → pd_id
        std::unordered_map<int,int> pdByForm;
        // NEXT_ASSEMBLY_USAGE_OCCURRENCE('','name','',#parent,#child,$)
        // → child_pd_id → instance name
        std::unordered_map<int,std::string> nauoNameByPD;

        for (const auto& e : entities) {
            const std::string& t = e.type;
            if (t == "PRODUCT_DEFINITION_FORMATION_WITH_SPECIFIED_SOURCE" ||
                t == "PRODUCT_DEFINITION_FORMATION") {
                int prodId = ExtractRef(GetNthParam(e.params, 2));
                if (prodId >= 0) formByProduct[prodId] = e.id;
            } else if (t == "PRODUCT_DEFINITION") {
                int formId = ExtractRef(GetNthParam(e.params, 2));
                if (formId >= 0) pdByForm[formId] = e.id;
            } else if (t == "NEXT_ASSEMBLY_USAGE_OCCURRENCE") {
                std::string n    = ExtractString(GetNthParam(e.params, 1));
                int         cpd  = ExtractRef(GetNthParam(e.params, 4));
                if (cpd >= 0 && !n.empty()) nauoNameByPD[cpd] = n;
            }
        }

        // --- Apply substitutions ----------------------------------------------
        for (const auto& e : entities) {
            if (e.type != "PRODUCT") continue;
            if (ExtractString(GetNthParam(e.params, 0)) != "0") continue;

            std::string newName;

            // Try MSB path first: product → pdform → pd → pdMsbName
            auto it1 = formByProduct.find(e.id);
            if (it1 != formByProduct.end()) {
                auto it2 = pdByForm.find(it1->second);
                if (it2 != pdByForm.end()) {
                    auto itMsb = pdMsbName.find(it2->second);
                    if (itMsb != pdMsbName.end()) newName = itMsb->second;

                    // NAUO fallback
                    if (newName.empty()) {
                        auto itNauo = nauoNameByPD.find(it2->second);
                        if (itNauo != nauoNameByPD.end() &&
                            !LooksLikeFilePath(itNauo->second))
                            newName = itNauo->second;
                    }
                }
            }

            if (newName.empty() || newName == "0") continue;

            // Rebuild entity: replace only the first two string params (name, id).
            // Keep params[2..] unchanged via GetParamsFrom.
            std::string esc      = EscapeStepString(newName);
            std::string restPars = GetParamsFrom(e.params, 2);
            std::string newText  = "#" + std::to_string(e.id)
                                 + "=PRODUCT('" + esc + "','" + esc + "',"
                                 + restPars + ");\n";
            patches.push_back({e.byteStart, e.byteEnd, newText});
        }
    }

    // -----------------------------------------------------------------------
    // Fix 3: HOOPS compat — strip OVER_RIDING_STYLED_ITEM entities and
    //        rebuild the MDGPR to reference only the base STYLED_ITEM.
    // -----------------------------------------------------------------------
    if (fixHoopsCompat && content.find("HOOPS Exchange") != std::string::npos) {
        std::unordered_set<int> overridingIds;
        for (const auto& e : entities) {
            if (e.type != "OVER_RIDING_STYLED_ITEM") continue;
            overridingIds.insert(e.id);
            patches.push_back({e.byteStart, e.byteEnd, ""});
        }

        if (!overridingIds.empty()) {
            for (const auto& e : entities) {
                if (e.type != "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION")
                    continue;

                // params = '',(items-tuple),#context
                std::string p0 = GetNthParam(e.params, 0); // ''
                std::string p1 = GetNthParam(e.params, 1); // (refs...)
                std::string p2 = GetNthParam(e.params, 2); // #context

                auto allRefs = ParseRefList(p1);
                std::vector<int> kept;
                for (int ref : allRefs)
                    if (!overridingIds.count(ref)) kept.push_back(ref);

                if (kept.size() == allRefs.size()) break; // nothing to change

                std::string newItems = "(";
                for (size_t i = 0; i < kept.size(); ++i) {
                    if (i) newItems += ",";
                    newItems += "#" + std::to_string(kept[i]);
                }
                newItems += ")";

                std::string newText =
                    "#" + std::to_string(e.id)
                    + "=MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION("
                    + p0 + "," + newItems + "," + p2 + ");\n";
                patches.push_back({e.byteStart, e.byteEnd, newText});
                break; // only one MDGPR expected
            }
        }
    }

    // -----------------------------------------------------------------------
    // Apply patches and write output
    // -----------------------------------------------------------------------
    std::sort(patches.begin(), patches.end(),
              [](const Patch& a, const Patch& b){ return a.start < b.start; });

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs) return false;

    size_t pos = 0;
    for (const auto& patch : patches) {
        if (patch.start > pos)
            ofs.write(content.c_str() + pos, patch.start - pos);
        if (!patch.text.empty())
            ofs.write(patch.text.c_str(), patch.text.size());
        pos = patch.end;
    }
    if (pos < content.size())
        ofs.write(content.c_str() + pos, content.size() - pos);

    return ofs.good();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool PatchStepFileText(
    const std::string& inputPath,
    const std::string& outputPath,
    bool fixNames,
    bool fixHoopsCompat)
{
    std::ifstream ifs(inputPath, std::ios::binary);
    if (!ifs) return false;
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    return PatchContentImpl(content, outputPath, fixNames, fixHoopsCompat);
}

bool PatchStepContent(
    const std::string& content,
    const std::string& outputPath,
    bool fixNames,
    bool fixHoopsCompat)
{
    return PatchContentImpl(content, outputPath, fixNames, fixHoopsCompat);
}

} // namespace StepFixerNative
