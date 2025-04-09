#include "cfg_generation_action.h"
#include "cfg_generation_action.h"
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/CompilerInstance.h>

CFGGenerationConsumer::CFGGenerationConsumer(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs)
    : m_graphs(graphs) {}

void CFGGenerationConsumer::HandleTranslationUnit(clang::ASTContext& Context) {
    using namespace clang::ast_matchers;

    // Match all function declarations
    auto matcher = functionDecl().bind("function");
    
    struct MatchHandler : public MatchFinder::MatchCallback {
        std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs;
        
        explicit MatchHandler(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& g) : graphs(g) {}

        void run(const MatchFinder::MatchResult& Result) override {
            const auto* func = Result.Nodes.getNodeAs<clang::FunctionDecl>("function");
            if (func && func->hasBody()) {
                auto cfg = GraphGenerator::generateCFG(func);
                if (cfg) {
                    graphs.push_back(std::move(cfg));
                }
            }
        }
    };

    MatchHandler handler(m_graphs);
    MatchFinder finder;
    finder.addMatcher(matcher, &handler);
    finder.matchAST(Context);
}

CFGGenerationAction::CFGGenerationAction(std::vector<std::unique_ptr<GraphGenerator::CFGGraph>>& graphs)
    : m_graphs(graphs) {}

std::unique_ptr<clang::ASTConsumer> CFGGenerationAction::CreateASTConsumer(
    clang::CompilerInstance& Compiler, llvm::StringRef) {
    return std::make_unique<CFGGenerationConsumer>(m_graphs);
}