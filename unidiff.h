#include <functional>
#include "dtl.hpp"
#include "Diff.hpp"
#include "functors.hpp"
#include "variables.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using dtl::Diff;
using dtl::elemInfo;
using dtl::uniHunk;


static std::vector<string> splitLine(const string &s) {
    vector<string> lines;
    stringstream inputs(s);
    string line;
    while (std::getline(inputs, line)) {
        lines.push_back(line);
    }
    return lines;
}

static void diff_file_by_lines(const string &alines, const string &blines) {
    vector<string> ALines, BLines;
    using sesElem = std::pair<string, dtl::elemInfo>;
    ALines = splitLine(alines);
    BLines = splitLine(blines);
    Diff<string> diff(ALines, BLines);
    diff.onHuge();
    diff.compose();
    uniHunk<sesElem> hunk;
    diff.composeUnifiedHunks();
    diff.printUnifiedFormat();
}


