#ifndef CFG_NODE_H
#define CFG_NODE_H

#include <string>
#include <vector>
#include <memory>

class CFGNode {
public:
    enum NodeType {
        ENTRY,
        EXIT,
        BASIC_BLOCK,
        CONDITIONAL,
        FUNCTION_CALL
    };

    CFGNode(const std::string& content, NodeType type);

    // Node identification and content
    std::string getContent() const;
    NodeType getType() const;
    std::string getTypeString() const;

    // Control flow management
    void addSuccessor(std::shared_ptr<CFGNode> node);
    void addPredecessor(std::shared_ptr<CFGNode> node);

    // Get successors and predecessors
    std::vector<std::shared_ptr<CFGNode>> getSuccessors() const;
    std::vector<std::shared_ptr<CFGNode>> getPredecessors() const;

    // Unique identifier
    std::string getUniqueId() const;

private:
    std::string m_content;
    NodeType m_type;
    std::vector<std::shared_ptr<CFGNode>> m_successors;
    std::vector<std::shared_ptr<CFGNode>> m_predecessors;
    std::string m_uniqueId;

    static size_t s_nodeCounter;
};

#endif // CFG_NODE_H