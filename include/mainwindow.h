#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QListWidgetItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "cfg_analyzer.h"
#include "customgraphview.h"
#include "graph_generator.h"
#include "parser.h"
#include "ui_mainwindow.h"
#include "ast_extractor.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    static const int NodeItemType;
    static const int EdgeItemType;
    static const QString NodeTypeKey;
    static const QString EdgeTypeKey;

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    void handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result);
    void loadAndProcessJson(const QString& filePath);
    void initializeGraphviz();
    void safeInitialize();
    void startTextOnlyMode();
    bool tryInitializeView(bool tryHardware);
    bool testRendering();
    void visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph);


public slots:
    void handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph);
    void handleVisualizationError(const QString& error);

signals:
    void analysisComplete(const CFGAnalyzer::AnalysisResult& result);

private slots:
    void on_browseButton_clicked();
    void on_analyzeButton_clicked();
    void on_openFilesButton_clicked();
    void on_searchButton_clicked();
    void on_toggleFunctionGraph_clicked();
    void on_fileList_itemClicked(QListWidgetItem *item);
    void displayFunctionInfo(const QString& functionName);
    void onParseButtonClicked();
    void onParsingFinished(bool success);
    void onLoadJsonClicked();
    void onMergeCfgsClicked();
    void on_extractAstButton_clicked();
    void exportGraph();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setGraphTheme(int theme);
    void toggleNodeLabels(bool visible);
    void toggleEdgeLabels(bool visible);
    void connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to);
    void dumpSceneInfo();
    void verifyScene();
    void addItemToScene(QGraphicsItem* item);
    void switchLayoutAlgorithm(int index);
    void onErrorOccurred(const QString& message);

private:
    Ui::MainWindow *ui;
    QStringList m_loadedFiles;
    QSet<QString> m_functionNames;
    QGraphicsScene* m_scene = nullptr;
    QThread* m_analysisThread;
    CustomGraphView* m_graphView = nullptr;
    Parser m_parser;
    ASTExtractor m_astExtractor;
    std::shared_ptr<GraphGenerator::CFGGraph> generateFunctionCFG(const QString& filePath, 
        const QString& functionName);
    
    std::shared_ptr<GraphGenerator::CFGGraph> parseDotToCFG(const QString& dotContent);

    enum LayoutAlgorithm {
        Hierarchical,
        ForceDirected,
        Circular
    };
    
    struct Theme {
        QColor nodeColor;
        QColor edgeColor;
        QColor textColor;
        QColor backgroundColor;
    };
    
    LayoutAlgorithm m_currentLayoutAlgorithm;
    Theme m_currentTheme;
    std::shared_ptr<GraphGenerator::CFGGraph> m_currentGraph;

    void createNode();
    void createEdge();
    void applyGraphLayout();
    void setUiEnabled(bool enabled);
    void visualizeFunction(const QString& functionName);
    void applyGraphTheme();
    void setupGraphLayout();
    void highlightFunction(const QString& functionName);
    void setupGraphView();
};

#endif // MAINWINDOW_H