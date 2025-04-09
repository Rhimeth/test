#include "mainwindow.h"
#include "cfg_analyzer.h"
#include "ui_mainwindow.h"
#include "visualizer.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPainter>
#include <QPrinter>
#include <QThreadPool>
#include <QPageLayout>
#include <QPageSize>
#include <QSvgGenerator>
#include <QBrush>
#include <QPen>
#include <QTimer>
#include <QFuture>
#include <exception>
#include <QtConcurrent>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QRandomGenerator>
#include <QMutex>
#include <clang/Frontend/ASTUnit.h>
#include <cmath>
#include <QCheckBox>
#include <QOpenGLWidget>
#include <QSurfaceFormat>

const int MainWindow::NodeItemType = QGraphicsItem::UserType + 1;
const int MainWindow::EdgeItemType = QGraphicsItem::UserType + 2;
const QString MainWindow::NodeTypeKey = "NodeType";
const QString MainWindow::EdgeTypeKey = "EdgeType";

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_scene(nullptr),
    m_analysisThread(nullptr),
    m_graphView(nullptr),
    m_currentLayoutAlgorithm(Hierarchical)
{
    if (QStandardPaths::findExecutable("dot").isEmpty()) {
        qWarning() << "GraphViz 'dot' executable not found in PATH.";
    } else {
        qDebug() << "'dot' found:" << QStandardPaths::findExecutable("dot");
    }

    ui->setupUi(this);

    m_currentTheme = {
        Qt::white,      // nodeColor
        Qt::black,      // edgeColor
        Qt::black,      // textColor
        Qt::white       // backgroundColor
    };
    
    // Remove placeholder if exists
    if (ui->graphicsView) {
        ui->verticalLayout->removeWidget(ui->graphicsView);
        delete ui->graphicsView;
        ui->graphicsView = nullptr;
    }
    
    // Direct initialization
    setupGraphView();
    
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    // Verify initialization
    if (!m_graphView || !m_graphView->scene()) {
        qCritical() << "Graph view initialization failed!";
        QMessageBox::critical(this, "Fatal Error", "Failed to initialize graph view");
        QCoreApplication::exit(1);
    }
    
    // Connect all signals
    connect(ui->browseButton, &QPushButton::clicked, this, &MainWindow::on_browseButton_clicked);
    connect(ui->analyzeButton, &QPushButton::clicked, this, &MainWindow::on_analyzeButton_clicked);
    connect(ui->openFilesButton, &QPushButton::clicked, this, &MainWindow::on_openFilesButton_clicked);
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::on_searchButton_clicked);
    connect(ui->toggleFunctionGraph, &QPushButton::clicked, this, &MainWindow::on_toggleFunctionGraph_clicked);
    connect(ui->fileList, &QListWidget::itemClicked, this, &MainWindow::on_fileList_itemClicked);
    connect(ui->loadJsonButton, &QPushButton::clicked, this, &MainWindow::onLoadJsonClicked);
    connect(ui->mergeCfgsButton, &QPushButton::clicked, this, &MainWindow::onMergeCfgsClicked);

    connect(ui->extractAstButton, &QPushButton::clicked, 
            this, &MainWindow::on_extractAstButton_clicked);

    // Initial UI state
    setUiEnabled(true);
}

void MainWindow::safeInitialize() {
    if (!tryInitializeView(true)) {
        qWarning() << "Hardware acceleration failed, trying software fallback";
        
        if (!tryInitializeView(false)) {
            qCritical() << "All graphics initialization failed";
            startTextOnlyMode();
        }
    }
}

bool MainWindow::tryInitializeView(bool tryHardware) {
    // Cleanup any existing views
    if (m_graphView) {
        m_graphView->setScene(nullptr);
        delete m_graphView;
        m_graphView = nullptr;
    }
    if (m_scene) {
        delete m_scene;
        m_scene = nullptr;
    }

    try {
        // 1. Create basic scene
        m_scene = new QGraphicsScene(this);
        m_scene->setBackgroundBrush(Qt::white);
        
        // 2. Configure view based on rendering mode
        m_graphView = new CustomGraphView(centralWidget());
        
        if (tryHardware) {
            m_graphView->setViewport(new QOpenGLWidget());
        } else {
            QWidget* simpleViewport = new QWidget();
            simpleViewport->setAttribute(Qt::WA_OpaquePaintEvent);
            simpleViewport->setAttribute(Qt::WA_NoSystemBackground);
            m_graphView->setViewport(simpleViewport);
        }
        
        // 3. Connect scene and view
        m_graphView->setScene(m_scene);
        
        // 4. Add to layout
        if (!centralWidget()->layout()) {
            centralWidget()->setLayout(new QVBoxLayout());
        }
        centralWidget()->layout()->addWidget(m_graphView);
        
        // 5. Test rendering
        return testRendering();
        
    } catch (...) {
        return false;
    }
}

bool MainWindow::testRendering() {
    // Add test item
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    
    // Try rendering to an image
    QImage testImg(100, 100, QImage::Format_ARGB32);
    QPainter painter(&testImg);
    m_scene->render(&painter);
    painter.end();
    
    // Verify some pixels changed
    return testImg.pixelColor(50, 50) != QColor(Qt::white);
}

void MainWindow::startTextOnlyMode() {
    qDebug() << "Starting in text-only mode";
    ui->graphicsView->hide();
    
    // Connect to the analysisComplete signal
    connect(this, &MainWindow::analysisComplete, this, 
        [this](const CFGAnalyzer::AnalysisResult& result) {
            ui->reportTextEdit->setPlainText(QString::fromStdString(result.dotOutput));
        });
}

void MainWindow::createNode() {
    if (!m_scene) return;
    
    QGraphicsEllipseItem* nodeItem = new QGraphicsEllipseItem(0, 0, 50, 50);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable);
    m_scene->addItem(nodeItem);
    
    // Center view on new item
    QTimer::singleShot(0, this, [this, nodeItem]() {
        if (m_graphView && nodeItem->scene()) {
            m_graphView->centerOn(nodeItem);
        }
    });
}

void MainWindow::createEdge() {
    // 1. Thread safety check
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    
    // 2. Validate scene and view existence
    if (!m_graphView || !m_graphView->scene()) {
        qWarning() << "Cannot create edge - graph view or scene not initialized";
        return;
    }

    // 3. Create edge with proper initialization
    QGraphicsLineItem* edgeItem = new QGraphicsLineItem();
    edgeItem->setData(MainWindow::EdgeItemType, 1);
    
    // 4. Configure edge properties
    edgeItem->setPen(QPen(Qt::black, 2));
    edgeItem->setFlag(QGraphicsItem::ItemIsSelectable);
    edgeItem->setZValue(-1); // Ensure edges render below nodes

    // 5. Safe addition to scene
    try {
        m_graphView->scene()->addItem(edgeItem);
        qDebug() << "Edge created - scene items:" << m_graphView->scene()->items().size();
    } catch (const std::exception& e) {
        qCritical() << "Failed to add edge:" << e.what();
        delete edgeItem;
    }
}

void MainWindow::connectNodesWithEdge(QGraphicsEllipseItem* from, QGraphicsEllipseItem* to) {
    if (!from || !to || !m_scene) return;

    // Calculate line between node centers
    QPointF fromCenter = from->mapToScene(from->rect().center());
    QPointF toCenter = to->mapToScene(to->rect().center());
    
    QGraphicsLineItem* edge = new QGraphicsLineItem(QLineF(fromCenter, toCenter));
    edge->setData(EdgeItemType, 1);
    edge->setPen(QPen(Qt::black, 2));
    edge->setZValue(-1); // Render behind nodes
    
    m_scene->addItem(edge);
}

void MainWindow::addItemToScene(QGraphicsItem* item)
{
    if (!m_scene) {
        qWarning() << "No active scene - deleting item";
        delete item;
        return;
    }

    try {
        m_scene->addItem(item);
    } catch (...) {
        qCritical() << "Failed to add item to scene";
        delete item;
    }
}

void MainWindow::setupGraphView()
{
    qDebug() << "=== Starting graph view setup ===";
    
    // 1. Clean existing resources
    if (m_scene) {
        m_scene->clear();
        delete m_scene;
    }
    if (m_graphView) {
        centralWidget()->layout()->removeWidget(m_graphView);
        delete m_graphView;
    }

    // 2. Create new scene with test content
    m_scene = new QGraphicsScene(this);
    QGraphicsRectItem* testItem = m_scene->addRect(0, 0, 100, 100, 
        QPen(Qt::red), QBrush(Qt::blue));
    testItem->setFlag(QGraphicsItem::ItemIsMovable);

    // 3. Configure view with software rendering
    m_graphView = new CustomGraphView(centralWidget());
    m_graphView->setViewport(new QWidget()); // Force software
    m_graphView->setScene(m_scene); // This sets both QGraphicsView's scene and CustomGraphView's m_scene
    m_graphView->setRenderHint(QPainter::Antialiasing, false);

    // 4. Add to layout
    if (!centralWidget()->layout()) {
        centralWidget()->setLayout(new QVBoxLayout());
    }
    centralWidget()->layout()->addWidget(m_graphView);

    qDebug() << "=== Graph view test setup complete ===";
    qDebug() << "Test item at:" << testItem->scenePos();
    qDebug() << "Viewport type:" << m_graphView->viewport()->metaObject()->className();
}

void MainWindow::visualizeCFG(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (!graph) {
        qWarning() << "Null CFGGraph provided!";
        return;
    }

    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());

    if (!m_graphView || !m_graphView->scene()) {
        qWarning() << "Graphics view not initialized";
        return;
    }

    try {
        std::string dotContent = Visualizer::generateDotRepresentation(graph.get());
        QString qDotContent = QString::fromStdString(dotContent);
        
        if (!m_graphView->parseDotFormat(qDotContent)) {
            throw std::runtime_error("Failed to parse DOT content");
        }

        // Store the graph
        m_currentGraph = graph;

        QTimer::singleShot(50, this, [this]() {
            if (m_graphView && m_graphView->scene()) {
                m_graphView->applyHierarchicalLayout();
                m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
                                     Qt::KeepAspectRatio);
            }
        });

    } catch (const std::exception& e) {
        qCritical() << "Visualization error:" << e.what();
        handleVisualizationError(QString::fromStdString(e.what()));
    }
}

std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::parseDotToCFG(const QString& dotContent)
{
    auto graph = std::make_shared<GraphGenerator::CFGGraph>();
        
    // Initialize regular expressions with properly escaped patterns
    QRegularExpression nodeRegex(R"(^\s*(\d+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression edgeRegex(R"(^\s*(\d+)\s*->\s*(\d+)\s*\[([^\]]*)\]\s*;?\s*$)");
    QRegularExpression labelRegex(R"~(label\s*=\\s*"([^"]*)")~");
    QRegularExpression colorRegex(R"~(color\s*=\s*"?(red|blue|green|black|white|gray)"?)~");
    QRegularExpression shapeRegex(R"~(shape\s*=\s*"?(box|ellipse|diamond|circle)"?)~");

    // Verify regex validity
    auto checkRegex = [](const QRegularExpression& re, const QString& name) {
        if (!re.isValid()) {
            qCritical() << "Invalid" << name << "regex:" << re.errorString() << "Pattern:" << re.pattern();
            return false;
        }
        return true;
    };

    if (!checkRegex(nodeRegex, "node") || 
        !checkRegex(edgeRegex, "edge") ||
        !checkRegex(labelRegex, "label") ||
        !checkRegex(colorRegex, "color") ||
        !checkRegex(shapeRegex, "shape")) {
        return graph;
    }

    // Split and process DOT content
    QStringList lines = dotContent.split('\n', Qt::SkipEmptyParts);
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        // Skip comments and graph declarations
        if (trimmed.startsWith("//") || trimmed.startsWith("/*") || 
            trimmed.startsWith("digraph") || trimmed.startsWith("}") || 
            trimmed.isEmpty()) {
            continue;
        }
        
        // Parse node
        auto nodeMatch = nodeRegex.match(trimmed);
        if (nodeMatch.hasMatch()) {
            bool ok;
            int id = nodeMatch.captured(1).toInt(&ok);
            if (!ok) continue;
            
            graph->addNode(id);
            
            QString attributes = nodeMatch.captured(2);
            auto labelMatch = labelRegex.match(attributes);
            if (labelMatch.hasMatch()) {
                graph->addStatement(id, labelMatch.captured(1).toStdString());
            }
            
            auto colorMatch = colorRegex.match(attributes);
            if (colorMatch.hasMatch() && colorMatch.captured(1) == "red") {
                graph->markNodeAsThrowingException(id);
            }
            
            auto shapeMatch = shapeRegex.match(attributes);
            if (shapeMatch.hasMatch() && shapeMatch.captured(1) == "box") {
                graph->markNodeAsTryBlock(id);
            }
            continue;
        }
        
        // Parse edge
        auto edgeMatch = edgeRegex.match(trimmed);
        if (edgeMatch.hasMatch()) {
            bool ok1, ok2;
            int fromId = edgeMatch.captured(1).toInt(&ok1);
            int toId = edgeMatch.captured(2).toInt(&ok2);
            if (!ok1 || !ok2) continue;
            
            graph->addEdge(fromId, toId);
            
            QString attributes = edgeMatch.captured(3);
            auto colorMatch = colorRegex.match(attributes);
            if (colorMatch.hasMatch() && colorMatch.captured(1) == "red") {
                graph->addExceptionEdge(fromId, toId);
            }
        }
    }
    
    return graph;
}

void MainWindow::loadAndProcessJson(const QString& filePath) 
{
    // Verify file exists
    if (!QFile::exists(filePath)) {
        qWarning() << "JSON file does not exist:" << filePath;
        QMessageBox::warning(this, "Error", "JSON file not found: " + filePath);
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open JSON file:" << file.errorString();
        QMessageBox::warning(this, "Error", "Could not open JSON file: " + file.errorString());
        return;
    }

    // Read and parse JSON
    QJsonParseError parseError;
    QByteArray jsonData = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error at offset" << parseError.offset << ":" << parseError.errorString();
        QMessageBox::warning(this, "JSON Error", 
                           QString("Parse error at position %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString()));
        return;
    }

    if (doc.isNull()) {
        qWarning() << "Invalid JSON document";
        QMessageBox::warning(this, "Error", "Invalid JSON document");
        return;
    }

    // Process the JSON data
    try {
        QJsonObject jsonObj = doc.object();
        
        // Example processing - adapt to your needs
        if (jsonObj.contains("nodes") && jsonObj["nodes"].isArray()) {
            QJsonArray nodes = jsonObj["nodes"].toArray();
            for (const QJsonValue& node : nodes) {
                if (node.isObject()) {
                    QJsonObject nodeObj = node.toObject();
                    // Process each node
                }
            }
        }
        
        // Update UI or visualization
        QMetaObject::invokeMethod(this, [this, jsonObj]() {
            // Update your graph view here
            m_graphView->parseJson(QJsonDocument(jsonObj).toJson());
            statusBar()->showMessage("JSON loaded successfully", 3000);
        });
        
    } catch (const std::exception& e) {
        qCritical() << "JSON processing error:" << e.what();
        QMessageBox::critical(this, "Processing Error", 
                            QString("Error processing JSON: %1").arg(e.what()));
    }
}

void MainWindow::initializeGraphviz()
{
    QString dotPath = QStandardPaths::findExecutable("dot");
    if (dotPath.isEmpty()) {
        qCritical() << "Graphviz 'dot' not found in PATH";
        QMessageBox::critical(this, "Error", 
                            "Graphviz 'dot' executable not found.\n"
                            "Please install Graphviz and ensure it's in your PATH.");
        startTextOnlyMode();
        return;
    }
    
    qDebug() << "Found Graphviz dot at:" << dotPath;
    setupGraphView();
}

void MainWindow::onParseButtonClicked()
{
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Parsing file...");

    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {
            // Read file content
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                throw std::runtime_error("Could not open file: " + filePath.toStdString());
            }
            
            QString dotContent = file.readAll();
            file.close();
            
            // Parse DOT content
            auto graph = parseDotToCFG(dotContent); // graph is now shared_ptr
            
            // Count nodes and edges
            int nodeCount = 0;
            int edgeCount = 0;
            for (const auto& [id, node] : graph->getNodes()) {
                nodeCount++;
                edgeCount += node.successors.size();
            }
            
            QString report = QString("Parsed CFG from DOT file\n\n")
                           + QString("File: %1\n").arg(filePath)
                           + QString("Nodes: %1\n").arg(nodeCount)
                           + QString("Edges: %1\n").arg(edgeCount);
            
            // Update UI in main thread
            QMetaObject::invokeMethod(this, [this, report, graph]() mutable {
                ui->reportTextEdit->setPlainText(report);
                visualizeCFG(graph); // Pass the shared_ptr directly
                setUiEnabled(true);
                statusBar()->showMessage("Parsing completed", 3000);
            });
            
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Error", QString("Parsing failed: %1").arg(e.what()));
                setUiEnabled(true);
                statusBar()->showMessage("Parsing failed", 3000);
            });
        }
    });
}

void MainWindow::onParsingFinished(bool success)
{
    // Additional post-parsing logic if needed
    if (success) {
        qDebug() << "Parsing completed successfully";
    } else {
        qDebug() << "Parsing failed";
    }
}

void MainWindow::applyGraphTheme() {
    QColor normalNodeColor = Qt::white;
    QColor tryBlockColor = QColor(173, 216, 230);
    QColor throwBlockColor = QColor(240, 128, 128);
    QColor normalEdgeColor = Qt::black;
    QColor exceptionEdgeColor = Qt::red;
}

void MainWindow::setupGraphLayout() {
    if (!m_graphView) return;

    switch (m_currentLayoutAlgorithm) {
        case Hierarchical: 
            m_graphView->applyHierarchicalLayout(); 
            break;
        case ForceDirected: 
            m_graphView->applyForceDirectedLayout(); 
            break;
        case Circular: 
            m_graphView->applyCircularLayout(); 
            break;
    }
}

void MainWindow::applyGraphLayout()
{
    if (!m_graphView) return;

    switch (m_currentLayoutAlgorithm) {
        case Hierarchical: 
            m_graphView->applyHierarchicalLayout(); 
            break;
        case ForceDirected: 
            m_graphView->applyForceDirectedLayout(); 
            break;
        case Circular: 
            m_graphView->applyCircularLayout(); 
            break;
    }
    
    // Optional: Fit the view after applying layout
    if (m_graphView->scene()) {
        m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
}

void MainWindow::highlightFunction(const QString& functionName) {
    if (!m_graphView) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            bool highlight = false;
            foreach (QGraphicsItem* child, item->childItems()) {
                if (auto text = dynamic_cast<QGraphicsTextItem*>(child)) {
                    if (text->toPlainText().contains(functionName, Qt::CaseInsensitive)) {
                        highlight = true;
                        break;
                    }
                }
            }
            
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                QBrush brush = ellipse->brush();
                brush.setColor(highlight ? Qt::yellow : m_currentTheme.nodeColor);
                ellipse->setBrush(brush);
            }
        }
    }
}

void MainWindow::exportGraph() {
    QString fileName = QFileDialog::getSaveFileName(this, "Export Graph",
        "", "PNG Images (*.png);;PDF Files (*.pdf);;SVG Files (*.svg)");
    
    if (fileName.isEmpty()) return;

    if (fileName.endsWith(".png")) {
        QImage image(m_graphView->sceneRect().size().toSize(), QImage::Format_ARGB32);
        QPainter painter(&image);
        m_graphView->render(&painter);
        image.save(fileName);
    }
    else if (fileName.endsWith(".pdf")) {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFileName(fileName);
        QPainter painter(&printer);
        m_graphView->render(&painter);
    }
    else if (fileName.endsWith(".svg")) {
        QSvgGenerator generator;
        generator.setFileName(fileName);
        QPainter painter(&generator);
        m_graphView->render(&painter);
    }
}

void MainWindow::zoomIn() {
    m_graphView->scale(1.2, 1.2);
}

void MainWindow::zoomOut() {
    m_graphView->scale(1/1.2, 1/1.2);
}

void MainWindow::resetZoom() {
    m_graphView->resetTransform();
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
}

    void MainWindow::on_browseButton_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select Source File");
    if (!filePath.isEmpty()) {
        ui->filePathEdit->setText(filePath);
    }
}

void MainWindow::on_analyzeButton_clicked() {
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Analyzing file...");

    QFuture<void> future = QtConcurrent::run([this, filePath]() {
        try {
            // Create analyzer instance with fully qualified name
            CFGAnalyzer::CFGAnalyzer analyzer;
            auto result = analyzer.analyze(filePath.toStdString());
            
            // Update UI in main thread
            QMetaObject::invokeMethod(this, [this, result]() {
                emit analysisComplete(result);
                handleAnalysisResult(result);
                setUiEnabled(true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                QMessageBox::critical(this, "Analysis Error", 
                                    QString("Analysis failed: %1").arg(e.what()));
                setUiEnabled(true);
                statusBar()->showMessage("Analysis failed", 3000);
            });
        }
    });
}

void MainWindow::handleAnalysisResult(const CFGAnalyzer::AnalysisResult& result) {
    if (!result.success) {
        ui->reportTextEdit->setPlainText(QString::fromStdString(result.report));
        QMessageBox::critical(this, "Analysis Error", 
                            QString::fromStdString(result.report));
        return;
    }

    // Handle DOT output if available
    if (!result.dotOutput.empty()) {
        try {
            auto graph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            m_currentGraph = graph; // Now using shared_ptr
            visualizeCFG(graph);
        } catch (...) {
            qWarning() << "Failed to visualize CFG";
        }
    }

    // Handle JSON output if available
    if (!result.jsonOutput.empty()) {
        m_graphView->parseJson(QString::fromStdString(result.jsonOutput).toUtf8());
    }

    statusBar()->showMessage("Analysis completed", 3000);
}

void MainWindow::on_extractAstButton_clicked() {
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false);
    ui->reportTextEdit->clear();
    statusBar()->showMessage("Extracting AST...");

    QtConcurrent::run([this, filePath]() {
        try {
            // Create analyzer instance with fully qualified name
            CFGAnalyzer::CFGAnalyzer analyzer;
            // Pass QString directly without conversion
            auto result = analyzer.analyzeFile(filePath);
            
            // Update UI in main thread
            QMetaObject::invokeMethod(this, [this, result]() {
                handleAnalysisResult(result);
                setUiEnabled(true);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                ui->reportTextEdit->setPlainText(QString("Error: %1").arg(e.what()));
                setUiEnabled(true);
                statusBar()->showMessage("Extraction failed", 3000);
            });
        }
    });
}

void MainWindow::displayFunctionInfo(const QString& input) {

    // Handle function name case
    if (!m_currentGraph) {
        ui->reportTextEdit->append("No CFG loaded");
        return;
    }

    // Search for the function in the current CFG
    bool found = false;
    const auto& nodes = m_currentGraph->getNodes();
    
    for (const auto& [id, node] : nodes) {
        if (QString::fromStdString(node.functionName).contains(input, Qt::CaseInsensitive)) {
            found = true;
            
            // Display basic function info
            ui->reportTextEdit->append(QString("Function: %1").arg(QString::fromStdString(node.functionName)));
            ui->reportTextEdit->append(QString("Node ID: %1").arg(id));
            ui->reportTextEdit->append(QString("Label: %1").arg(QString::fromStdString(node.label)));
            
            // Display statements if available
            if (!node.statements.empty()) {
                ui->reportTextEdit->append("\nStatements:");
                for (const auto& stmt : node.statements) {
                    ui->reportTextEdit->append(QString::fromStdString(stmt));
                }
            }
            
            // Display successors
            if (!node.successors.empty()) {
                ui->reportTextEdit->append("\nConnects to:");
                for (int successor : node.successors) {
                    QString edgeType = m_currentGraph->isExceptionEdge(id, successor) 
                        ? " (exception edge)" 
                        : "";
                    ui->reportTextEdit->append(QString("  -> Node %1%2")
                        .arg(successor)
                        .arg(edgeType));
                }
            }
            
            ui->reportTextEdit->append("------------------");
        }
    }

    if (!found) {
        ui->reportTextEdit->append(QString("Function '%1' not found in CFG").arg(input));
    }
}

void MainWindow::on_fileList_itemClicked(QListWidgetItem *item)
{
    if (item) {
        ui->filePathEdit->setText(item->text());
        on_analyzeButton_clicked();
    }
}

void MainWindow::on_searchButton_clicked()
{
    QString searchText = ui->search->text().trimmed();
    if (!searchText.isEmpty()) {
        // First try to highlight existing nodes
        m_graphView->highlightFunction(searchText);
        
        // Then try to visualize the function if not found
        if (!m_graphView->hasHighlightedItems()) {
            visualizeFunction(searchText);
        }
    }
}

void MainWindow::on_toggleFunctionGraph_clicked()
{
    // Implement your toggle functionality here
    static bool showFullGraph = true;
    m_graphView->toggleGraphDisplay(!showFullGraph);
    showFullGraph = !showFullGraph;
}

void MainWindow::onLoadJsonClicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open CFG JSON", 
                                                  "", "JSON Files (*.json)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray jsonData = file.readAll();
            file.close();
            
            if (!m_loadedFiles.contains(fileName)) {
                m_loadedFiles.append(fileName);
                ui->fileList->addItem(fileName);
            }
            
            m_graphView->parseJson(jsonData);
            m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
        } else {
            QMessageBox::warning(this, "Error", "Could not open file: " + fileName);
        }
    }
}

void MainWindow::onMergeCfgsClicked() {
    if (m_loadedFiles.size() < 2) {
        QMessageBox::warning(this, "Merge Error", "Need at least 2 CFGs to merge");
        return;
    }
    
    QJsonObject mergedGraph;
    QJsonArray nodes;
    QJsonArray edges;
    
    foreach (const QString &filePath, m_loadedFiles) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            
            if (!doc.isNull()) {
                QJsonObject obj = doc.object();
                nodes.append(obj["nodes"].toArray());
                edges.append(obj["edges"].toArray());
            }
        }
    }
    
    mergedGraph["nodes"] = nodes;
    mergedGraph["edges"] = edges;
    
    // Display merged graph
    m_graphView->parseJson(QJsonDocument(mergedGraph).toJson());
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), 
                         Qt::KeepAspectRatio);
}

void MainWindow::setGraphTheme(int theme)
{
    if (!m_graphView) return;

    struct Theme {
        QColor background;
        QColor node;
        QColor edge;
        QColor text;
    };

    const QVector<Theme> themes = {
        {Qt::white, QColor(240, 240, 240), Qt::black, Qt::black}, // Light
        {QColor(53, 53, 53), QColor(80, 80, 80), Qt::white, Qt::white}, // Dark
        {QColor(240, 248, 255), QColor(173, 216, 230), QColor(0, 0, 139), Qt::black} // Blue
    };

    if (theme < 0 || theme >= themes.size()) return;
    
    m_graphView->setBackgroundBrush(themes[theme].background);
    m_graphView->setThemeColors(themes[theme].node, themes[theme].edge, themes[theme].text);
}

void MainWindow::toggleNodeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::NodeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }
}

void MainWindow::toggleEdgeLabels(bool visible) {
    if (!m_graphView || !m_graphView->scene()) return;
    
    foreach (QGraphicsItem* item, m_graphView->scene()->items()) {
        if (item->data(MainWindow::EdgeItemType).toInt() == 1) {
            foreach (QGraphicsItem* child, item->childItems()) {
                if (dynamic_cast<QGraphicsTextItem*>(child)) {
                    child->setVisible(visible);
                }
            }
        }
    }
}

void MainWindow::switchLayoutAlgorithm(int index)
{
    if (!m_graphView) return;

    switch(index) {
    case 0: m_graphView->applyHierarchicalLayout(); break;
    case 1: m_graphView->applyForceDirectedLayout(); break;
    case 2: m_graphView->applyCircularLayout(); break;
    default: break;
    }
    
    m_graphView->fitInView(m_graphView->scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void MainWindow::visualizeFunction(const QString& functionName) 
{
    QString filePath = ui->filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a file first");
        return;
    }

    setUiEnabled(false); // Disable UI during processing
    statusBar()->showMessage("Generating CFG for function...");

    QtConcurrent::run([this, filePath, functionName]() {
        try {
            auto cfgGraph = generateFunctionCFG(filePath, functionName);
            QMetaObject::invokeMethod(this, [this, cfgGraph]() {
                handleVisualizationResult(cfgGraph);
            });
        } catch (const std::exception& e) {
            QMetaObject::invokeMethod(this, [this, e]() {
                handleVisualizationError(QString::fromStdString(e.what()));
            });
        }
    });
}

std::shared_ptr<GraphGenerator::CFGGraph> MainWindow::generateFunctionCFG(
    const QString& filePath, const QString& functionName)
{
    try {
        // Create analyzer instance and use its public methods instead
        CFGAnalyzer::CFGAnalyzer analyzer;
        
        // Use the public API to analyze the file - pass QString directly
        auto result = analyzer.analyzeFile(filePath);
        
        if (!result.success) {
            throw std::runtime_error("Failed to analyze file: " + result.report);
        }
        
        // Create an empty CFG graph to start with
        auto cfgGraph = std::make_shared<GraphGenerator::CFGGraph>();
        
        // For simplicity, we'll extract the graph from the DOT output
        // if the analyzer provides it
        if (!result.dotOutput.empty()) {
            // Use our DOT parser to convert the DOT output to a CFG graph
            cfgGraph = parseDotToCFG(QString::fromStdString(result.dotOutput));
            
            // Filter graph to only include the requested function
            if (!functionName.isEmpty()) {
                // We need to filter nodes to only include those from the function
                // This is a simplified approach - the actual implementation
                // might require more sophisticated filtering based on your graph structure
                auto filteredGraph = std::make_shared<GraphGenerator::CFGGraph>();
                
                const auto& nodes = cfgGraph->getNodes();
                for (const auto& [id, node] : nodes) {
                    if (QString::fromStdString(node.functionName)
                            .compare(functionName, Qt::CaseInsensitive) == 0) {
                        // Copy this node and its edges to the filtered graph
                        filteredGraph->addNode(id);
                        for (int successor : node.successors) {
                            filteredGraph->addEdge(id, successor);
                        }
                    }
                }
                
                cfgGraph = filteredGraph;
            }
        }
        
        return cfgGraph;
    }
    catch (const std::exception& e) {
        qCritical() << "Error generating function CFG:" << e.what();
        throw;
    }
}

void MainWindow::handleVisualizationResult(std::shared_ptr<GraphGenerator::CFGGraph> graph)
{
    if (graph) {
        m_currentGraph = graph;
        visualizeCFG(graph); // Now matches the signature
    }
    setUiEnabled(true);
    statusBar()->showMessage("Visualization complete", 3000);
}

void MainWindow::handleVisualizationError(const QString& error)
{
    QMessageBox::warning(this, "Visualization Error", error);
    setUiEnabled(true);
    statusBar()->showMessage("Visualization failed", 3000);
}

void MainWindow::onErrorOccurred(const QString& message) {
    ui->reportTextEdit->setPlainText("Error: " + message);
    setUiEnabled(true);
    QMessageBox::critical(this, "Analysis Error", message);
}

void MainWindow::on_openFilesButton_clicked()
{
    // Implementation for opening multiple files
    QStringList filePaths = QFileDialog::getOpenFileNames(this, "Select Source Files");
    if (!filePaths.isEmpty()) {
        ui->fileList->clear();
        for (const QString &path : filePaths) {
            ui->fileList->addItem(path);
        }
    }
}

void MainWindow::setUiEnabled(bool enabled)
{
    QList<QWidget*> widgets = {
        ui->browseButton, ui->analyzeButton, ui->openFilesButton,
        ui->searchButton, ui->toggleFunctionGraph, ui->fileList,
        ui->loadJsonButton, ui->mergeCfgsButton
    };
    
    foreach (QWidget* widget, widgets) {
        widget->setEnabled(enabled);
    }
    
    if (enabled) {
        statusBar()->showMessage("Ready");
    } else {
        statusBar()->showMessage("Processing...");
    }
}

void MainWindow::dumpSceneInfo() {
    if (!m_scene) {
        qDebug() << "Scene: nullptr";
        return;
    }
    
    qDebug() << "=== Scene Info ===";
    qDebug() << "Items count:" << m_scene->items().size();
    qDebug() << "Scene rect:" << m_scene->sceneRect();
    
    if (m_graphView) {
        qDebug() << "View transform:" << m_graphView->transform();
        qDebug() << "View visible items:" << m_graphView->items().size();
    }
}

void MainWindow::verifyScene()
{
    if (!m_scene || !m_graphView) {
        qCritical() << "Invalid scene or view!";
        return;
    }

    if (m_graphView->scene() != m_scene) {
        qCritical() << "Scene/view mismatch!";
        m_graphView->setScene(m_scene);
    }
}

MainWindow::~MainWindow()
{
    // Ensure all threads are stopped first
    if (m_analysisThread && m_analysisThread->isRunning()) {
        m_analysisThread->quit();
        m_analysisThread->wait();
    }

    // Clear scene first (may contain items with mutexes)
    if (m_scene) {
        m_scene->clear();
        delete m_scene;
        m_scene = nullptr;
    }

    // Then remove view
    if (m_graphView) {
        if (centralWidget() && centralWidget()->layout()) {
            centralWidget()->layout()->removeWidget(m_graphView);
        }
        delete m_graphView;
        m_graphView = nullptr;
    }
    
    delete ui;
}