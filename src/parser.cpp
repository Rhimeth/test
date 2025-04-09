#include "parser.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/CFG.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <regex>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <QDebug>

using namespace clang;
namespace fs = std::filesystem;

// Cache for statement strings to improve performance
static std::unordered_map<std::string, std::string> stmtCache;

class Parser::FunctionVisitor : public RecursiveASTVisitor<FunctionVisitor> {
public:
    explicit FunctionVisitor(ASTContext* context) : context(context) {}
    
    bool VisitFunctionDecl(FunctionDecl* decl) {
        if (!decl->hasBody()) return true;
        
        auto loc = context->getSourceManager().getPresumedLoc(decl->getLocation());
        if (!loc.isValid()) return true;

        try {
            functions.push_back({
                decl->getNameAsString(),
                loc.getFilename(),
                static_cast<unsigned>(loc.getLine()),
                true
            });
            functionDecls[decl->getNameAsString()] = decl;
        } catch (const std::exception& e) {
            qWarning() << "Error processing function declaration:" << e.what();
        }
        return true;
    }
    
    std::vector<FunctionInfo> getFunctions() const { return functions; }
    
    FunctionDecl* getFunctionDecl(const std::string& name) const {
        auto it = functionDecls.find(name);
        return it != functionDecls.end() ? it->second : nullptr;
    }

private:
    ASTContext* context;
    std::vector<FunctionInfo> functions;
    std::map<std::string, FunctionDecl*> functionDecls;
};

std::vector<Parser::FunctionInfo> Parser::extractFunctions(const std::string& filePath) {
    std::vector<FunctionInfo> functions;
    
    if (!fs::exists(filePath)) {
        qWarning() << "File not found:" << filePath.c_str();
        return functions;
    }

    if (ASTContext* context = parseFile(filePath)) {
        try {
            FunctionVisitor visitor(context);
            visitor.TraverseDecl(context->getTranslationUnitDecl());
            functions = visitor.getFunctions();
        } catch (const std::exception& e) {
            qCritical() << "Error extracting functions:" << e.what();
        }
    }
    return functions;
}

std::vector<Parser::FunctionCFG> Parser::extractAllCFGs(const std::string& filePath) {
    std::vector<FunctionCFG> cfgs;
    
    if (!fs::exists(filePath)) {
        qWarning() << "File not found:" << filePath.c_str();
        return cfgs;
    }

    if (ASTContext* context = parseFile(filePath)) {
        try {
            FunctionVisitor visitor(context);
            visitor.TraverseDecl(context->getTranslationUnitDecl());
            
            for (const auto& funcInfo : visitor.getFunctions()) {
                if (FunctionDecl* decl = visitor.getFunctionDecl(funcInfo.name)) {
                    FunctionCFG cfg;
                    cfg.functionName = funcInfo.name;
                    
                    std::unique_ptr<CFG> cfgPtr = CFG::buildCFG(
                        decl, 
                        decl->getBody(), 
                        context, 
                        CFG::BuildOptions()
                    );
                    
                    if (!cfgPtr) continue;
                    
                    // Process nodes
                    for (const CFGBlock* block : *cfgPtr) {
                        CFGNode node;
                        node.id = block->getBlockID();
                        
                        std::string label;
                        if (block->empty()) {
                            label = (node.id == 0) ? "ENTRY" : "EXIT";
                        } else {
                            for (const auto& element : *block) {
                                if (element.getKind() == CFGElement::Statement) {
                                    const Stmt* stmt = element.castAs<CFGStmt>().getStmt();
                                    std::string stmtStr;
                                    llvm::raw_string_ostream os(stmtStr);
                                    stmt->printPretty(os, nullptr, PrintingPolicy(context->getLangOpts()));
                                    os.flush();
                                    
                                    if (stmtCache.count(stmtStr)) {
                                        label += stmtCache[stmtStr];
                                    } else {
                                        stmtCache[stmtStr] = stmtStr + "\n";
                                        label += stmtCache[stmtStr];
                                    }
                                    
                                    if (node.code.empty()) node.code = stmtStr;
                                }
                            }
                        }
                        
                        node.label = label.empty() ? "Empty Block" : label;
                        SourceLocation loc = block->empty() ? SourceLocation() :
                            (block->front().getKind() == CFGElement::Statement) ?
                                block->front().castAs<CFGStmt>().getStmt()->getBeginLoc() :
                                SourceLocation();
                        auto presumedLoc = context->getSourceManager().getPresumedLoc(loc);
                        node.line = presumedLoc.isValid() ? presumedLoc.getLine() : 0;
                        
                        cfg.nodes.push_back(node);
                    }
                    
                    // Process edges
                    for (const CFGBlock* block : *cfgPtr) {
                        unsigned sourceId = block->getBlockID();
                        unsigned edgeIdx = 0;
                        
                        for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
                            if (const CFGBlock* succ = *it) {
                                CFGEdge edge;
                                edge.sourceId = sourceId;
                                edge.targetId = succ->getBlockID();
                                
                                if (block->succ_size() > 1) {
                                    edge.label = (edgeIdx == 0) ? "True" : "False";
                                } else {
                                    edge.label = "Unconditional";
                                }
                                
                                cfg.edges.push_back(edge);
                                edgeIdx++;
                            }
                        }
                    }
                    
                    cfgs.push_back(cfg);
                }
            }
        } catch (const std::exception& e) {
            qCritical() << "Error extracting CFGs:" << e.what();
        }
    }
    
    return cfgs;
}

std::string Parser::generateDOT(const FunctionCFG& cfg) {
    std::ostringstream dot;
    dot << "digraph \"" << cfg.functionName << "\" {\n";
    dot << "  node [shape=rectangle, fontname=\"Courier\", fontsize=10];\n";
    dot << "  edge [fontsize=8];\n\n";
    
    // Add nodes
    for (const auto& node : cfg.nodes) {
        dot << "  " << node.id << " [";
        
        if (node.id == 0) {
            dot << "label=\"ENTRY\", shape=diamond, style=filled, fillcolor=palegreen";
        } else if (node.id == 1 && cfg.nodes.size() > 1) {
            dot << "label=\"EXIT\", shape=diamond, style=filled, fillcolor=palegreen";
        } else {
            // Escape special characters
            std::string label = node.label;
            std::replace(label.begin(), label.end(), '"', '\'');
            label = std::regex_replace(label, std::regex("\n"), "\\n");
            
            dot << "label=\"" << label << "\"";
            
            // Highlight complex nodes
            if (node.label.find('\n') != std::string::npos) {
                dot << ", style=filled, fillcolor=lemonchiffon";
            }
        }
        
        dot << "];\n";
    }
    
    // Add edges
    for (const auto& edge : cfg.edges) {
        dot << "  " << edge.sourceId << " -> " << edge.targetId;
        
        if (!edge.label.empty()) {
            dot << " [label=\"" << edge.label << "\"";
            
            if (edge.label == "True" || edge.label == "False") {
                dot << ", color=blue";
            }
            
        }
        
        dot << ";\n";
    }

    dot << "}\n";
    return dot.str();
}

std::unique_ptr<clang::ASTUnit> Parser::parseFileWithAST(const std::string& filename) {
    if (!fs::exists(filename)) {
        qWarning() << "File not found:" << filename.c_str();
        return nullptr;
    }

    // Find Clang resource directory
    std::string resourceDir;
    if (llvm::sys::fs::exists("/usr/lib/llvm-14/lib/clang/14.0.0/include")) {
        resourceDir = "/usr/lib/llvm-14/lib/clang/14.0.0/include";
    } else {
        // Fallback to searching common paths
        for (const auto& entry : fs::directory_iterator("/usr/lib/llvm")) {
            if (entry.path().string().find("clang") != std::string::npos) {
                resourceDir = entry.path().string() + "/include";
                break;
            }
        }
    }

    std::vector<std::string> args = {
        "-std=c++17",
        "-I.",
        "-ferror-limit=2",
        "-fno-exceptions",
        "-O0",
        "-Wno-everything",
        "-resource-dir=" + resourceDir
    };

    auto ast = clang::tooling::buildASTFromCodeWithArgs("", args, filename);
    
    if (!ast) {
        qCritical() << "AST generation failed for:" << filename.c_str();
        if (ast && ast->getDiagnostics().getNumErrors()) {
            qCritical() << "Diagnostics:";
            // Replace the iterator-based approach with direct reporting
            const auto& diags = ast->getDiagnostics();
            qCritical() << "Found" << diags.getNumErrors() << "errors and" 
                       << diags.getNumWarnings() << "warnings";
            
            // We can't iterate through diagnostics directly, so log this info
            qCritical() << "See compiler output for details";
        }
    }
    return ast;
}

clang::ASTContext* Parser::ThreadLocalState::parse(const std::string& filePath) {
    if (!fs::exists(filePath)) {
        qWarning() << "File not found:" << filePath.c_str();
        return nullptr;
    }

    try {
        if (!compiler) {
            compiler = std::make_unique<clang::CompilerInstance>();
            setupCompiler();
        }

        consumer = std::make_unique<ASTStoringConsumer>();
        
        std::vector<std::string> args = {
            "-x", "c++", 
            "-std=c++17",
            "-I.", 
            "-I/usr/include",
            "-I/usr/local/include",
            filePath
        };
        
        std::vector<const char*> cArgs;
        for (const auto& arg : args) {
            cArgs.push_back(arg.c_str());
        }

        if (!CompilerInvocation::CreateFromArgs(
            compiler->getInvocation(),
            cArgs,
            compiler->getDiagnostics())) {
            qWarning() << "Failed to create compiler invocation";
            return nullptr;
        }

        compiler->setASTConsumer(std::move(consumer));
        SyntaxOnlyAction action;
        if (!compiler->ExecuteAction(action)) {
            qWarning() << "Failed to execute parse action";
            return nullptr;
        }

        return consumer->Context;
    } catch (const std::exception& e) {
        qCritical() << "Parser exception:" << e.what();
        return nullptr;
    }
}

void Parser::ThreadLocalState::setupCompiler() {
    compiler->createDiagnostics();
    compiler->createFileManager();
    compiler->createSourceManager(compiler->getFileManager());
    compiler->createPreprocessor(TU_Complete);
    compiler->createASTContext();
}

Parser::Parser() {
    // Initialize any necessary members here
}

// Destructor implementation
Parser::~Parser() {
    // Clean up any resources if needed
}

// parseFile implementation
clang::ASTContext* Parser::parseFile(const std::string& filePath) {
    thread_local ThreadLocalState state;
    return state.parse(filePath);
}

Parser::ThreadLocalState::~ThreadLocalState() {
    // Clean up compiler resources
    if (compiler) {
        compiler->getDiagnostics().Reset();
        // Release other resources if needed
    }
    // consumer will be automatically deleted by unique_ptr
}