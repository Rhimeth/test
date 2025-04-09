#ifndef GRAPH_GENERATOR_H
#define GRAPH_GENERATOR_H

#include "parser.h" 
#include <set>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <clang/AST/Stmt.h>
#include <clang/Analysis/CFG.h>
#include <clang/AST/Decl.h>
#include <nlohmann/json.hpp>

namespace GraphGenerator {
    using json = nlohmann::json;

    // Forward declaration of the CFGGraph class
    class CFGGraph;

    // Use the forward declaration for the function signatures
    std::unique_ptr<CFGGraph> generateCFG(const std::vector<std::string>& sourceFiles);
    std::unique_ptr<CFGGraph> generateCFG(const clang::FunctionDecl* FD);
    std::unique_ptr<CFGGraph> generateCustomCFG(const clang::FunctionDecl* FD);
    std::unique_ptr<CFGGraph> generateCFG(const Parser::FunctionInfo& functionInfo, clang::ASTContext* context);
    std::string getStmtString(const clang::Stmt* S);

    // Typedef for Graph if needed
    using Graph = CFGGraph;

    struct CFGNode {
        int id;
        std::string label;
        std::string functionName;
        std::set<int> successors;
        std::vector<std::string> statements;
        
        // Default constructor
        CFGNode() : id(-1), label(""), functionName("") {}
            
        // Existing constructor
        CFGNode(int nodeId, const std::string& lbl = "", const std::string& fnName = "")
            : id(nodeId), label(lbl), functionName(fnName) {}
    };
        
    struct CFGEdge {
        int sourceID;
        int targetID;
        bool isExceptionEdge;
        std::string label;
    };

    class CFGGraph {
    public:
        // Methods remain the same
        void writeToDotFile(const std::string& filename) const;
        void writeToJsonFile(const std::string& filename, const json& astJson, const json& functionCallJson);
        std::string getNodeLabel(int nodeID) const;

        // New methods for exception handling
        void addStatement(int nodeID, const std::string& stmt);
        void addExceptionEdge(int sourceID, int targetID);
        bool isExceptionEdge(int sourceID, int targetID) const;
        void markNodeAsTryBlock(int nodeID);
        void markNodeAsThrowingException(int nodeID);
        bool isNodeTryBlock(int nodeID) const;
        bool isNodeThrowingException(int nodeID) const;

        void addNode(int id, const std::string& label);
        size_t getNodeCount() const;
        size_t getEdgeCount() const;

        // Get function names
        std::vector<std::string> getFunctionNames() const {
            std::vector<std::string> names;
            for (const auto& pair : nodes) {
                if (!pair.second.functionName.empty()) {
                    names.push_back(pair.second.functionName);
                }
            }
            return names;
        }

        // Existing methods remain the same
        void addNode(int nodeID) {
            if (nodes.find(nodeID) == nodes.end()) {
                nodes[nodeID] = CFGNode(nodeID, "Block " + std::to_string(nodeID));
            }
        }
        
        void addStatementToNode(int nodeID, const std::string& stmt) {
            if (nodes.find(nodeID) == nodes.end()) {
                addNode(nodeID);
            }
            nodes[nodeID].statements.push_back(stmt);
        }       
        
        void addEdge(int fromID, int toID) {
            if (nodes.find(fromID) == nodes.end()) {
                addNode(fromID);
            }
            nodes[fromID].successors.insert(toID);
        }    
        
        const std::map<int, CFGNode>& getNodes() const noexcept { 
            return nodes; 
        }
        
    private:
        std::map<int, CFGNode> nodes;
        std::set<std::pair<int, int>> exceptionEdges;
        std::set<int> tryBlocks;
        std::set<int> throwingBlocks;
    };
}

#endif // GRAPH_GENERATOR_H