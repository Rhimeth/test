#include "graph_generator.h"
#include "parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/raw_ostream.h>
#include <clang/AST/Stmt.h>
#include <nlohmann/json.hpp>
#include <clang/AST/ASTContext.h>

using json = nlohmann::json;

namespace GraphGenerator {

    std::string getStmtString(const clang::Stmt* S) {
        if (!S) return "NULL";
        std::string stmtStr;
        llvm::raw_string_ostream stream(stmtStr);
        S->printPretty(stream, nullptr, clang::PrintingPolicy(clang::LangOptions()));
        stream.flush();
        return stmtStr;
    }

    void extractStatementsFromBlock(const clang::CFGBlock* block, CFGGraph* graph) {
        for (const auto& element : *block) {
            if (element.getKind() == clang::CFGElement::Statement) {
                const clang::Stmt* stmt = element.castAs<clang::CFGStmt>().getStmt();
                std::string stmtStr = getStmtString(stmt);
                graph->addStatement(block->getBlockID(), stmtStr);
            }
        }
    }

    void handleTryAndCatch(const clang::CFGBlock* block, CFGGraph* graph, std::map<const clang::Stmt*, clang::CFGBlock*>& stmtToBlock) {
        for (const auto& element : *block) {
            if (element.getKind() != clang::CFGElement::Statement) continue;

            const clang::Stmt* stmt = element.castAs<clang::CFGStmt>().getStmt();
            int blockID = block->getBlockID();

            if (llvm::isa<clang::CXXTryStmt>(stmt)) {
                graph->markNodeAsTryBlock(blockID);

                const auto* tryStmt = llvm::dyn_cast<clang::CXXTryStmt>(stmt);
                for (unsigned i = 0; i < tryStmt->getNumHandlers(); ++i) {
                    const clang::CXXCatchStmt* catchStmt = tryStmt->getHandler(i);
                    if (stmtToBlock.count(catchStmt)) {
                        graph->addExceptionEdge(blockID, stmtToBlock[catchStmt]->getBlockID());
                    }
                }
            }
            if (llvm::isa<clang::CXXThrowExpr>(stmt)) {
                graph->markNodeAsThrowingException(blockID);
            }
        }
    }

    void handleSuccessors(const clang::CFGBlock* block, CFGGraph* graph) {
        int blockID = block->getBlockID();

        for (auto succ = block->succ_begin(); succ != block->succ_end(); ++succ) {
            if (*succ) {
                graph->addEdge(blockID, (*succ)->getBlockID());
            }
        }
    }

    std::unique_ptr<CFGGraph> generateCFG(const clang::FunctionDecl* FD) {
        if (!FD || !FD->hasBody()) return nullptr;
        
        // Handle template functions
        if (FD->getTemplatedKind() == clang::FunctionDecl::TK_FunctionTemplate) {
            llvm::errs() << "Skipping uninstantiated template function: " << FD->getNameAsString() << "\n";
            return nullptr;
        }

        // Handle template instantiations
        const clang::FunctionDecl* actualFD = FD;
        if (FD->isTemplateInstantiation()) {
            if (const clang::FunctionDecl* Pattern = FD->getTemplateInstantiationPattern()) {
                if (Pattern->hasBody()) {
                    actualFD = Pattern;
                }
            }
        }

        auto graph = std::make_unique<CFGGraph>();
        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            actualFD, 
            actualFD->getBody(), 
            &actualFD->getASTContext(), 
            clang::CFG::BuildOptions()
        );

        if (!cfg) {
            llvm::errs() << "Failed to build CFG for function: " << actualFD->getNameAsString() << "\n";
            return nullptr;
        }

        std::map<const clang::Stmt*, clang::CFGBlock*> stmtToBlock;
        for (auto* block : *cfg) {
            if (!block) continue;
            
            for (const auto& element : *block) {
                if (element.getKind() == clang::CFGElement::Statement) {
                    if (const auto* stmt = element.castAs<clang::CFGStmt>().getStmt()) {
                        stmtToBlock[stmt] = block;
                    }
                }
            }
        }


        for (const auto* block : *cfg) {
            if (!block) continue;
            
            graph->addNode(block->getBlockID());
            extractStatementsFromBlock(block, graph.get());
            handleTryAndCatch(block, graph.get(), stmtToBlock);
            handleSuccessors(block, graph.get());
        }

        return graph;
    }

    std::unique_ptr<CFGGraph> generateCustomCFG(const clang::FunctionDecl* FD) {
        if (!FD || !FD->hasBody()) return nullptr;
        
        auto graph = std::make_unique<CFGGraph>();
        
        std::unique_ptr<clang::CFG> cfg = clang::CFG::buildCFG(
            FD,
            FD->getBody(),
            &FD->getASTContext(),
            clang::CFG::BuildOptions()
        );
        
        if (!cfg) {
            llvm::errs() << "Failed to build custom CFG for function: " 
                        << FD->getNameAsString() << "\n";
            return nullptr;
        }

        return graph;
    }

    std::unique_ptr<CFGGraph> generateCFG(const Parser::FunctionInfo& functionInfo, clang::ASTContext* context) {
        if (!context) return nullptr;

        const clang::SourceManager& SM = context->getSourceManager();
        const clang::FunctionDecl* FD = nullptr;

        for (const auto* decl : context->getTranslationUnitDecl()->decls()) {
            if (const auto* funcDecl = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
                if (funcDecl->getNameAsString() != functionInfo.name) continue;
                
                auto loc = SM.getPresumedLoc(funcDecl->getLocation());
                if (!loc.isValid()) continue;
                
                // Compare file paths (handle system-specific path separators)
                std::string declFile = loc.getFilename();
                std::string targetFile = functionInfo.filename;
                
                // Simple path comparison - consider using llvm::sys::fs::equivalent() for robustness
                if (declFile == targetFile && loc.getLine() == functionInfo.line) {
                    FD = funcDecl;
                    break;
                }
            }
        }

        if (!FD) {
            llvm::errs() << "Failed to find function declaration: " 
                        << functionInfo.name << " at " 
                        << functionInfo.filename << ":" 
                        << functionInfo.line << "\n";
            return nullptr;
        }

        return generateCFG(FD);
    }

} // namespace GraphGenerator