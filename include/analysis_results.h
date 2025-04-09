// analysis_results.h
#ifndef ANALYSIS_RESULTS_H
#define ANALYSIS_RESULTS_H

#include <string>
#include <unordered_map>
#include <set>

namespace CFGAnalyzer {

struct AnalysisResults {
    std::string dotOutput;
    std::string analysisReport;
    std::unordered_map<std::string, std::set<std::string>> functionDependencies;
};

} // namespace CFGAnalyzer

#endif // ANALYSIS_RESULTS_H