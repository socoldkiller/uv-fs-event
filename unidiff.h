
#include "dtl.hpp"
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

static void unifiedDiff(const string &fp1, const string &fp2) {
    vector<string> ALines, BLines;
    using sesElem = std::pair<string, dtl::elemInfo>;
    ALines = splitLine(fp1);
    BLines = splitLine(fp2);
    Diff<string> diff(ALines, BLines);
    diff.onHuge();
    diff.compose();
    uniHunk<sesElem> hunk;
    diff.composeUnifiedHunks();
    diff.printUnifiedFormat();
}


