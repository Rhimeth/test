#include <iostream>
#include <fstream>
#include "cfg_analyzer.h"
#include "parser.h"
#include "graph_generator.h"
#include "visualizer.h"
#include <QString>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace CFGAnalyzer {

CFGVisitor::CFGVisitor(clang::ASTContext* Context,
                     const std::string& outputDir,
                     AnalysisResult& results)
    : Context(Context), 
      OutputDir(outputDir), 
      m_results(results) 
{
    if (!llvm::sys::fs::exists(outputDir)) {
        llvm::sys::fs::create_directory(outputDir);
    }
}

bool CFGVisitor::VisitFunctionDecl(clang::FunctionDecl* FD) {
    if (!FD || !FD->hasBody()) return true;
    
    clang::SourceManager& SM = Context->getSourceManager();
    if (!SM.isInMainFile(FD->getLocation())) return true;
    
    std::string funcName = FD->getQualifiedNameAsString();
    CurrentFunction = funcName;
    FunctionDependencies[funcName] = std::set<std::string>();
    
    std::string funcFilename = OutputDir + "/" + funcName + "_cfg.dot";
    auto cfgGraph = GraphGenerator::generateCFG(FD);
    if (cfgGraph) {
        Visualizer::exportToDot(cfgGraph.get(), funcFilename);
    }
    
    return true;
}

bool CFGVisitor::VisitCallExpr(clang::CallExpr* CE) {
    if (!CurrentFunction.empty() && CE) {
        if (auto* CalledFunc = CE->getDirectCallee()) {
            FunctionDependencies[CurrentFunction].insert(
                CalledFunc->getQualifiedNameAsString());
        }
    }
    return true;
}

void CFGVisitor::PrintFunctionDependencies() const {
    llvm::outs() << "Function Dependencies:\n";
    for (const auto& [caller, callees] : FunctionDependencies) {
        llvm::outs() << caller << " calls:\n";
        for (const auto& callee : callees) {
            llvm::outs() << "  - " << callee << "\n";
        }
    }
}

std::unordered_map<std::string, std::set<std::string>> 
CFGVisitor::GetFunctionDependencies() const {
    return FunctionDependencies;
}

void CFGVisitor::FinalizeCombinedFile() {
    std::string combinedFilename = OutputDir + "/combined_cfg.dot";
    if (llvm::sys::fs::exists(combinedFilename)) {
        std::ofstream outFile(combinedFilename, std::ios::app);
        if (outFile.is_open()) {
            outFile << "}\n";
            outFile.close();
        }
    }
    
    m_results.functionDependencies = FunctionDependencies;
}

CFGConsumer::CFGConsumer(clang::ASTContext* Context,
                       const std::string& outputDir,
                       AnalysisResult& results)
    : Visitor(std::make_unique<CFGVisitor>(Context, outputDir, results)) {}

void CFGConsumer::HandleTranslationUnit(clang::ASTContext& Context) {
    Visitor->TraverseDecl(Context.getTranslationUnitDecl());
    Visitor->FinalizeCombinedFile();
}

CFGAction::CFGAction(const std::string& outputDir,
                   AnalysisResult& results)
    : OutputDir(outputDir), m_results(results) {}

std::unique_ptr<clang::ASTConsumer> CFGAction::CreateASTConsumer(
    clang::CompilerInstance& CI, llvm::StringRef File) {
    return std::make_unique<CFGConsumer>(&CI.getASTContext(), OutputDir, m_results);
}

AnalysisResult CFGAnalyzer::analyze(const std::string& filename) {
    AnalysisResult result;
    std::vector<std::string> CommandLine = {
        "-std=c++17",
        "-I.",
        "-I/usr/include",
        "-I/usr/local/include"
    };

    auto Compilations = std::make_unique<clang::tooling::FixedCompilationDatabase>(
        ".", CommandLine);
    if (!Compilations) {
        result.report = "Failed to create compilation database";
        return result;
    }

    std::vector<std::string> Sources{filename};
    clang::tooling::ClangTool Tool(*Compilations, Sources);

    class CFGActionFactory : public clang::tooling::FrontendActionFactory {
    public:
        CFGActionFactory(AnalysisResult& results) : m_results(results) {}
        
        std::unique_ptr<clang::FrontendAction> create() override {
            return std::make_unique<CFGAction>("cfg_output", m_results);
        }
        
    private:
        AnalysisResult& m_results;
    };

    CFGActionFactory factory(m_results);
    int ToolResult = Tool.run(&factory);
    
    if (ToolResult != 0) {
        result.report = "Analysis failed with code: " + std::to_string(ToolResult);
        return result;
    }

    // Generate outputs
    {
        QMutexLocker locker(&m_analysisMutex);
        result.dotOutput = generateDotOutput(m_results);
        result.report = generateReport(m_results);
        result.success = true;
    }

    return result;
}

std::string CFGAnalyzer::generateDotOutput(const AnalysisResult& result) const {
    std::stringstream dotStream;
    dotStream << "digraph FunctionDependencies {\n"
              << "  node [shape=rectangle, style=filled, fillcolor=lightblue];\n"
              << "  edge [arrowsize=0.8];\n"
              << "  rankdir=LR;\n\n";

    for (const auto& [caller, callees] : result.functionDependencies) {
        dotStream << "  \"" << caller << "\";\n";
        for (const auto& callee : callees) {
            dotStream << "  \"" << caller << "\" -> \"" << callee << "\";\n";
        }
    }

    dotStream << "}\n";
    return dotStream.str();
}

AnalysisResult CFGAnalyzer::analyzeFile(const QString& filePath) {
    AnalysisResult result;
    try {
        std::string filename = filePath.toStdString();
        result = analyze(filename);
        
        if (!result.success) {
            return result;
        }
        
        // Generate JSON output
        json j;
        j["filename"] = filename;
        j["timestamp"] = getCurrentDateTime();
        j["functions"] = json::array();
        
        for (const auto& [func, calls] : result.functionDependencies) {
            json function;
            function["name"] = func;
            function["calls"] = calls;
            j["functions"].push_back(function);
        }
        
        result.jsonOutput = j.dump(2);
    }
    catch (const std::exception& e) {
        result.report = std::string("Analysis error: ") + e.what();
        result.success = false;
    }
    
    return result;
}

std::string CFGAnalyzer::getCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm tm;
    localtime_r(&in_time_t, &tm);
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string CFGAnalyzer::generateReport(const AnalysisResult& result) const {
    std::stringstream report;
    report << "CFG Analysis Report\n";
    report << "Generated: " << getCurrentDateTime() << "\n\n";
    report << "Function Dependencies:\n";
    
    for (const auto& [caller, callees] : result.functionDependencies) {
        report << caller << " calls:\n";
        for (const auto& callee : callees) {
            report << "  - " << callee << "\n";
        }
        report << "\n";
    }
    
    return report.str();
}

} // namespace CFGAnalyzer