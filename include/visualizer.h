#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <string>
#include "graph_generator.h"

namespace Visualizer {

std::string generateDotRepresentation(
    const GraphGenerator::CFGGraph* graph,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {}
);

bool exportToDot(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {}
);

enum class ExportFormat {
    DOT,
    PNG,
    SVG,
    PDF
};

bool exportGraph(
    const GraphGenerator::CFGGraph* graph,
    const std::string& filename,
    ExportFormat format = ExportFormat::DOT,
    bool showLineNumbers = true,
    bool simplifyGraph = false,
    const std::vector<int>& highlightPaths = {}
);

} // namespace Visualizer

#endif // VISUALIZER_H