#include "node.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

size_t CFGNode::s_nodeCounter = 0;

CFGNode::CFGNode(const std::string& content, NodeType type)
    : m_content(content), m_type(type)
{
    // Generate a unique identifier
    std::ostringstream oss;
    oss << "node_" << std::setw(4) << std::setfill('0') << s_nodeCounter++;
    m_uniqueId = oss.str();
}

std::string CFGNode::getContent() const {
    return m_content;
}

CFGNode::NodeType CFGNode::getType() const {
    return m_type;
}

std::string CFGNode::getTypeString() const {
    switch(m_type) {
        case ENTRY: return "Entry";
        case EXIT: return "Exit";
        case BASIC_BLOCK: return "Basic Block";
        case CONDITIONAL: return "Conditional";
        case FUNCTION_CALL: return "Function Call";
        default: return "Unknown";
    }
}

void CFGNode::addSuccessor(std::shared_ptr<CFGNode> node) {
    // Prevent duplicate successors
    if (std::find(m_successors.begin(), m_successors.end(), node) == m_successors.end()) {
        m_successors.push_back(node);
    }
}

void CFGNode::addPredecessor(std::shared_ptr<CFGNode> node) {
    // Prevent duplicate predecessors
    if (std::find(m_predecessors.begin(), m_predecessors.end(), node) == m_predecessors.end()) {
        m_predecessors.push_back(node);
    }
}

std::vector<std::shared_ptr<CFGNode>> CFGNode::getSuccessors() const {
    return m_successors;
}

std::vector<std::shared_ptr<CFGNode>> CFGNode::getPredecessors() const {
    return m_predecessors;
}

std::string CFGNode::getUniqueId() const {
    return m_uniqueId;
}