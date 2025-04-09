#include "visualizer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <QDebug>

namespace Visualizer {

std::string generateDotRepresentation(
    const GraphGenerator::CFGGraph* graph,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    if (!graph) {
        throw std::invalid_argument("Graph pointer cannot be null");
    }

    std::stringstream dot;
    dot << "digraph CFG {\n";
    dot << "  node [shape=box, fontname=\"Courier\", fontsize=10];\n";
    dot << "  edge [fontsize=8];\n";
    
    // Add nodes
    for (const auto& [id, node] : graph->getNodes()) {
        dot << "  " << id << " [label=\"";
        
        if (showLineNumbers) {
            // Comment out the line number code since the method doesn't existn-existent method
            // We can add this functionality back when we know the correct wayr > 0) {  // Assuming node has a lineNumber member
            // to retrieve line numbers from the graph<< ": ";
            // 
            // int lineNum = graph->getNodeLineNumber(id);
            // if (lineNum > 0) {
            //     dot << "Line " << lineNum << ": ";
            // }
        }
        dot << graph->getNodeLabel(id) << "\"";
        
        // Apply styles based on node properties
        if (graph->isNodeTryBlock(id)) {
            dot << ", style=filled, fillcolor=lightblue";
        }
        if (graph->isNodeThrowingException(id)) {
            dot << ", style=filled, fillcolor=lightcoral";
        }
        if (std::find(highlightPaths.begin(), highlightPaths.end(), id) != highlightPaths.end()) {
            dot << ", style=filled, fillcolor=yellow, penwidth=2";
        }
        if (simplifyGraph && node.successors.size() == 1) {
            dot << ", shape=ellipse";
        }
        if (node.successors.size() > 1) {
            dot << ", style=dashed, color=gray";
        }
        
        dot << "];\n";
    }
    
    // Add edges
    for (const auto& [id, node] : graph->getNodes()) {
        for (int succ : node.successors) {
            dot << "  " << id << " -> " << succ;
            
            if (graph->isExceptionEdge(id, succ)) {
                dot << " [color=red, style=dashed, label=\"exception\"]";
            } 
            // Remove or replace with alternative approach
            // else if (simplifyGraph && graph->isBackEdge(id, succ)) {
            else if (simplifyGraph && succ <= id) {  // Simple heuristic for back edges
                dot << " [color=blue, style=bold]";
            }
            
            dot << ";\n";
        }
    }
    
    dot << "}\n";
    return dot.str();
}

bool exportToDot(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    try {
        if (!graph) {
            qWarning() << "Cannot export null graph";
            return false;
        }

        std::ofstream outFile(filename);
        if (!outFile.is_open()) {
            qWarning() << "Failed to open file:" << filename.c_str();
            return false;
        }

        outFile << generateDotRepresentation(graph, showLineNumbers, simplifyGraph, highlightPaths);
        return true;
    } catch (const std::exception& e) {
        qCritical() << "Export failed:" << e.what();
        return false;
    }
}

bool exportGraph(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    ExportFormat format,
    bool showLineNumbers,
    bool simplifyGraph,
    const std::vector<int>& highlightPaths)
{
    if (format != ExportFormat::DOT) {
        qWarning() << "Only DOT export is currently supported";
        return false;
    }
    
    return exportToDot(graph, filename, showLineNumbers, simplifyGraph, highlightPaths);
}

} // namespace Visualizer