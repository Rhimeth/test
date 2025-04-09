#include "cfg_gui.h"
#include "cfg_analyzer.h"
#include "parser.h"
#include "graph_generator.h"
#include "visualizer.h"
#include <QSvgGenerator>
#include <QPageSize>
#include <QPageLayout>
#include <QProcess>
#include <QListWidget>
#include <QMessageBox>
#include <QFileDialog>
#include <QGraphicsView>
#include <QCheckBox>
#include <QGraphicsScene>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QFont>
#include <QBrush>
#include <QPen>
#include <QFile>
#include <QPrinter>
#include <QPainter>
#include <QTextStream>
#include <QDateTime>
#include <QRandomGenerator>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <cmath>

namespace CFGAnalyzer {

    CFGVisualizerWindow::CFGVisualizerWindow(QWidget *parent)
        : QMainWindow(parent),
        centralWidget(nullptr),
        tabWidget(nullptr),
        inputTab(nullptr),
        filePathEdit(nullptr),
        browseButton(nullptr),
        analyzeButton(nullptr),
        loadDotButton(nullptr),
        outputConsole(nullptr),
        loadedFilesList(nullptr),
        visualizationTab(nullptr),
        scene(nullptr),
        view(nullptr),
        layoutComboBox(nullptr),
        zoomInButton(nullptr),
        zoomOutButton(nullptr),
        resetZoomButton(nullptr),
        exportButton(nullptr),
        zoomFactor(1.0),
        currentLayoutAlgorithm(0) 
    {
        setupUI();
        setWindowTitle("CFG Analyzer");
        resize(1200, 800);
        
        // Setup menu
        QMenu* fileMenu = menuBar()->addMenu("&File");
        QAction* openAction = fileMenu->addAction("&Open...");
        connect(openAction, &QAction::triggered, this, &CFGVisualizerWindow::browseFile);
        fileMenu->addSeparator();
        QAction* exitAction = fileMenu->addAction("&Exit");
        connect(exitAction, &QAction::triggered, this, &QWidget::close);
        
        QMenu* helpMenu = menuBar()->addMenu("&Help");
        QAction* aboutAction = helpMenu->addAction("&About");
        connect(aboutAction, &QAction::triggered, this, &CFGVisualizerWindow::showAbout);
        
        statusBar()->showMessage("Ready");
    }

    CFGVisualizerWindow::~CFGVisualizerWindow() {
        // Clean up any resources if needed
        if (scene) {
            scene->clear();
        }
    }

    void CFGVisualizerWindow::updateFileList() {
        if (!loadedFilesList) {
            qWarning() << "Loaded files list widget is not initialized";
            return;
        }

        // Clear the current list
        loadedFilesList->clear();

        // Add any existing files to the list
        if (!currentFiles.isEmpty()) {
            loadedFilesList->addItems(currentFiles);
        }

        // Optionally highlight the most recently added file
        if (!currentFiles.isEmpty()) {
            QList<QListWidgetItem*> items = loadedFilesList->findItems(currentFiles.last(), Qt::MatchExactly);
            if (!items.isEmpty()) {
                loadedFilesList->setCurrentItem(items.first());
            }
        }

        // Update status bar
        statusBar()->showMessage(QString("Loaded %1 files").arg(currentFiles.size()), 3000);
    }

    QListWidget* loadedFilesList = nullptr;

    void CFGVisualizerWindow::setupUI() {
        centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        loadDotButton = new QPushButton("Load DOT File");

        QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
        
        // Create tab widget
        tabWidget = new QTabWidget();
        
        // Input tab
        inputTab = new QWidget();
        QVBoxLayout* inputLayout = new QVBoxLayout(inputTab);
        
        QHBoxLayout* fileSelectLayout = new QHBoxLayout();
        QLabel* fileLabel = new QLabel("Source File:");
        filePathEdit = new QLineEdit();
        browseButton = new QPushButton("Browse...");
        fileSelectLayout->addWidget(fileLabel);
        fileSelectLayout->addWidget(filePathEdit);
        fileSelectLayout->addWidget(browseButton);
        fileSelectLayout->addWidget(loadDotButton); // Added loadDotButton here after fileSelectLayout is created
        
        analyzeButton = new QPushButton("Analyze");
        analyzeButton->setMinimumHeight(40);
        
        QLabel* outputLabel = new QLabel("Analysis Output:");
        outputConsole = new QTextEdit();
        outputConsole->setReadOnly(true);
        outputConsole->setFont(QFont("Monospace", 10));

        loadedFilesList = new QListWidget(this);

        QPushButton* loadJsonButton = new QPushButton("Load JSON");
        QPushButton* mergeCfgsButton = new QPushButton("Merge CFGs");
        inputLayout->addWidget(loadJsonButton);
        inputLayout->addWidget(mergeCfgsButton);
        
        loadedFilesList = new QListWidget();
        inputLayout->addWidget(new QLabel("Loaded CFGs:"));
        inputLayout->addWidget(loadedFilesList);

        // Connect signals
        connect(loadJsonButton, &QPushButton::clicked, this, &CFGVisualizerWindow::loadJSON);
        connect(mergeCfgsButton, &QPushButton::clicked, this, &CFGVisualizerWindow::mergeCFGs);
        
        inputLayout->addLayout(fileSelectLayout);
        inputLayout->addWidget(analyzeButton);
        inputLayout->addWidget(outputLabel);
        inputLayout->addWidget(outputConsole);
        loadedFilesList = new QListWidget();
        
        // Visualization tab
        visualizationTab = new QWidget();
        QVBoxLayout* visLayout = new QVBoxLayout(visualizationTab);
        
        QHBoxLayout* controlsLayout = new QHBoxLayout();
        QLabel* layoutLabel = new QLabel("Layout:");
        layoutComboBox = new QComboBox();
        layoutComboBox->addItem("Force-Directed");
        layoutComboBox->addItem("Hierarchical");
        layoutComboBox->addItem("Circular");
        
        zoomInButton = new QPushButton("Zoom In");
        zoomOutButton = new QPushButton("Zoom Out");
        resetZoomButton = new QPushButton("Reset Zoom");
        exportButton = new QPushButton("Export");
        
        controlsLayout->addWidget(layoutLabel);
        controlsLayout->addWidget(layoutComboBox);
        controlsLayout->addStretch();
        controlsLayout->addWidget(zoomInButton);
        controlsLayout->addWidget(zoomOutButton);
        controlsLayout->addWidget(resetZoomButton);
        controlsLayout->addWidget(exportButton);
        
        scene = new QGraphicsScene(this);
        view = new QGraphicsView(scene);
        view->setRenderHint(QPainter::Antialiasing);
        view->setDragMode(QGraphicsView::ScrollHandDrag);
        
        visLayout->addLayout(controlsLayout);
        visLayout->addWidget(view);
        
        // Add tabs to tab widget
        tabWidget->addTab(inputTab, "Input & Analysis");
        tabWidget->addTab(visualizationTab, "Visualization");
        
        mainLayout->addWidget(tabWidget);

        QHBoxLayout* visControlsLayout = new QHBoxLayout();
        
        QCheckBox* nodeLabelsCheck = new QCheckBox("Show Node Labels");
        nodeLabelsCheck->setChecked(true);
        connect(nodeLabelsCheck, &QCheckBox::toggled, this, &CFGVisualizerWindow::toggleNodeLabels);
        
        QCheckBox* edgeLabelsCheck = new QCheckBox("Show Edge Labels");
        edgeLabelsCheck->setChecked(true);
        connect(edgeLabelsCheck, &QCheckBox::toggled, this, &CFGVisualizerWindow::toggleEdgeLabels);
        
        QComboBox* themeCombo = new QComboBox();
        themeCombo->addItems({"Light Theme", "Dark Theme", "High Contrast"});
        connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &CFGVisualizerWindow::setGraphTheme);
        
        visControlsLayout->addWidget(nodeLabelsCheck);
        visControlsLayout->addWidget(edgeLabelsCheck);
        visControlsLayout->addWidget(new QLabel("Theme:"));
        visControlsLayout->addWidget(themeCombo);
        visControlsLayout->addStretch();
        
        // Add file management buttons
        QPushButton* clearFilesBtn = new QPushButton("Clear All");
        QPushButton* removeFileBtn = new QPushButton("Remove Selected");
        QPushButton* fileInfoBtn = new QPushButton("File Info");
        
        connect(clearFilesBtn, &QPushButton::clicked, this, &CFGVisualizerWindow::clearLoadedFiles);
        connect(removeFileBtn, &QPushButton::clicked, this, &CFGVisualizerWindow::removeSelectedFile);
        connect(fileInfoBtn, &QPushButton::clicked, this, &CFGVisualizerWindow::showFileInfo);
        
        QHBoxLayout* fileMgmtLayout = new QHBoxLayout();
        fileMgmtLayout->addWidget(clearFilesBtn);
        fileMgmtLayout->addWidget(removeFileBtn);
        fileMgmtLayout->addWidget(fileInfoBtn);
        fileMgmtLayout->addStretch();
        
        // Add to main layout
        inputLayout->insertLayout(1, fileMgmtLayout);  // Adjust position as needed
        visLayout->insertLayout(1, visControlsLayout); // Add below existing controls
        
        // Connect signals and slots
        connect(browseButton, &QPushButton::clicked, this, &CFGVisualizerWindow::browseFile);
        connect(analyzeButton, &QPushButton::clicked, this, &CFGVisualizerWindow::analyzeFile);
        connect(zoomInButton, &QPushButton::clicked, this, &CFGVisualizerWindow::zoomIn);
        connect(zoomOutButton, &QPushButton::clicked, this, &CFGVisualizerWindow::zoomOut);
        connect(resetZoomButton, &QPushButton::clicked, this, &CFGVisualizerWindow::resetZoom);
        connect(exportButton, &QPushButton::clicked, this, &CFGVisualizerWindow::exportGraph);
        connect(layoutComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), 
                this, &CFGVisualizerWindow::switchLayoutAlgorithm);
        connect(loadDotButton, &QPushButton::clicked, this, &CFGVisualizerWindow::loadDotFile);

        if (loadedFilesList) {
            loadedFilesList->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(loadedFilesList, &QListWidget::customContextMenuRequested,
                [this](const QPoint& pos) {
                    QMenu contextMenu;
                    QAction* infoAction = contextMenu.addAction("Show Info");
                    QAction* removeAction = contextMenu.addAction("Remove");
                    QAction* exportAction = contextMenu.addAction("Export CFG");
                    
                    QAction* selected = contextMenu.exec(loadedFilesList->mapToGlobal(pos));
                    if (selected == infoAction) {
                        showFileInfo();
                    } else if (selected == removeAction) {
                        removeSelectedFile();
                    } else if (selected == exportAction) {
                        exportSelectedCFG();
                    }
                });
        }
    }

    void CFGVisualizerWindow::setupBasicUI() {
        // Main widget and layout
        QWidget* central = new QWidget(this);
        QVBoxLayout* mainLayout = new QVBoxLayout(central);
        
        // File list section
        QLabel* listLabel = new QLabel("Loaded CFG Files:");
        loadedFilesList = new QListWidget();

        // In mergeSelectedCfgs():
        if (loadedFilesList->selectedItems().isEmpty()) {
            QMessageBox::information(this, "Merge", "Please select CFGs to merge");
            return;
        }

        QStringList selectedFiles;
        foreach(QListWidgetItem* item, loadedFilesList->selectedItems()) {
            selectedFiles << item->text();
        }
        
        // Add sample files
        loadedFilesList->addItem("Vertopal.com_Rectangle_cfg.json");
        loadedFilesList->addItem("Vertopal.com_combined_cfg.json");
        
        // Action buttons
        QPushButton* loadJsonBtn = new QPushButton("Load JSON");
        QPushButton* mergeCfgsBtn = new QPushButton("Merge CFGs");
        
        // Add to layout
        mainLayout->addWidget(listLabel);
        mainLayout->addWidget(loadedFilesList);
        mainLayout->addWidget(loadJsonBtn);
        mainLayout->addWidget(mergeCfgsBtn);
        
        // Connect buttons
        connect(loadJsonBtn, &QPushButton::clicked, this, &CFGVisualizerWindow::loadJsonFile);
        connect(mergeCfgsBtn, &QPushButton::clicked, this, &CFGVisualizerWindow::mergeSelectedCfgs);
        
        // Final setup
        setCentralWidget(central);
        setWindowTitle("CFG Visualizer");
        resize(400, 300);
    }

    void CFGVisualizerWindow::loadJsonFile() {
        QString fileName = QFileDialog::getOpenFileName(this, "Open JSON File", "", "JSON Files (*.json);;All Files (*)");
        if (!fileName.isEmpty()) {
            QFile file(fileName);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray data = file.readAll();
                // Process JSON data
                file.close();
            }
        }
    }

    void CFGVisualizerWindow::mergeSelectedCfgs() {
        if (loadedFilesList->selectedItems().isEmpty()) {
            QMessageBox::information(this, "Merge", "Please select CFGs to merge");
            return;
        }
        
        QStringList selectedFiles;
        foreach(QListWidgetItem* item, loadedFilesList->selectedItems()) {
            selectedFiles << item->text();
        }
        
        // Your merge implementation here
        statusBar()->showMessage(
            QString("Merging %1 CFGs").arg(selectedFiles.count()), 
            3000
        );
    }

    // Add this new function
    void CFGVisualizerWindow::exportSelectedCFG() {
        if (!loadedFilesList || loadedFilesList->selectedItems().isEmpty()) {
            QMessageBox::warning(this, "Export Error", "No CFG selected to export");
            return;
        }

        QString selectedFile = loadedFilesList->currentItem()->text();
        QFileInfo fileInfo(selectedFile);
        
        QString fileName = QFileDialog::getSaveFileName(
            this,
            "Export CFG",
            fileInfo.baseName() + "_export",
            "DOT Files (*.dot);;PNG Images (*.png);;PDF Files (*.pdf);;SVG Files (*.svg)"
        );

        if (fileName.isEmpty()) return;

        if (fileName.endsWith(".dot", Qt::CaseInsensitive)) {
            QFile file(fileName);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                
                if (currentGraph) {
                    std::string dotContent = Visualizer::generateDotRepresentation(currentGraph.get());
                    out << QString::fromStdString(dotContent);
                    statusBar()->showMessage("CFG exported as DOT to " + fileName, 3000);
                } else {
                    QFile inputFile(selectedFile);
                    if (inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        out << inputFile.readAll();
                        statusBar()->showMessage("CFG file copied to " + fileName, 3000);
                    } else {
                        QMessageBox::warning(this, "Export Error", "Failed to read source CFG file");
                    }
                }
                file.close();
            } else {
                QMessageBox::warning(this, "Export Error", "Failed to create DOT file");
            }
        }
        else if (fileName.endsWith(".png", Qt::CaseInsensitive) || 
                fileName.endsWith(".pdf", Qt::CaseInsensitive) ||
                fileName.endsWith(".svg", Qt::CaseInsensitive)) {
            if (!scene || scene->items().isEmpty()) {
                QMessageBox::warning(this, "Export Error", "No visualization to export");
                return;
            }

            if (fileName.endsWith(".png", Qt::CaseInsensitive)) {
                QImage image(scene->sceneRect().size().toSize(), QImage::Format_ARGB32);
                image.fill(Qt::white);
                
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing);
                scene->render(&painter);
                
                if (image.save(fileName)) {
                    statusBar()->showMessage("CFG exported as PNG to " + fileName, 3000);
                } else {
                    QMessageBox::warning(this, "Export Error", "Failed to save PNG image");
                }
            }
            else if (fileName.endsWith(".pdf", Qt::CaseInsensitive)) {
                QPrinter printer(QPrinter::HighResolution);
                printer.setOutputFormat(QPrinter::PdfFormat);
                printer.setOutputFileName(fileName);
                printer.setPageSize(QPageSize(QPageSize::A4));
                printer.setPageOrientation(QPageLayout::Portrait);
                
                QPainter painter(&printer);
                painter.setRenderHint(QPainter::Antialiasing);
                scene->render(&painter);
                
                statusBar()->showMessage("CFG exported as PDF to " + fileName, 3000);
            }
            else if (fileName.endsWith(".svg", Qt::CaseInsensitive)) {
                QSvgGenerator svgGen;
                svgGen.setFileName(fileName);
                svgGen.setSize(scene->sceneRect().size().toSize());
                svgGen.setViewBox(scene->sceneRect());
                
                QPainter painter(&svgGen);
                painter.setRenderHint(QPainter::Antialiasing);
                scene->render(&painter);
                
                statusBar()->showMessage("CFG exported as SVG to " + fileName, 3000);
            }
        } else {
            QMessageBox::warning(this, "Export Error", "Unsupported file format");
        }
    }

    void CFGVisualizerWindow::mergeCFGs() {
        if (!loadedFilesList || loadedFilesList->count() == 0) {
            QMessageBox::warning(this, "Merge Error", "No CFGs loaded to merge");
            return;
        }
        
        // Implement your merging logic here
        // Example:
        // 1. Get selected items from loadedFilesList
        // 2. Process them
        // 3. Update visualization
        QMessageBox::information(this, "Merge CFGs", 
                            QString("Merging %1 CFGs").arg(loadedFilesList->count()));
    }

    void CFGVisualizerWindow::loadJSON() {
        QString filePath = QFileDialog::getOpenFileName(this, "Open File",
            "", "Source Files (*.c *.cpp *.h *.hpp);;JSON Files (*.json);;DOT Files (*.dot);;All Files (*)");

        if (!filePath.isEmpty()) {
            filePathEdit->setText(filePath);
        }
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Error", "Failed to open JSON file.");
            return;
        }
        
        QByteArray jsonData = file.readAll();
        file.close();
        
        QJsonDocument document = QJsonDocument::fromJson(jsonData);
        if (document.isNull()) {
            QMessageBox::warning(this, "Error", "Invalid JSON format.");
            return;
        }
        
        QJsonObject rootObj = document.object();
        
        // Check if JSON contains a dot_file field
        if (rootObj.contains("dot_file")) {
            QString dotFilePath = rootObj["dot_file"].toString();
            QFile dotFile(dotFilePath);
            if (dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&dotFile);
                QString dotContent = in.readAll();
                dotFile.close();
                
                outputConsole->clear();
                outputConsole->append("Loaded DOT file from JSON: " + dotFilePath);
                tabWidget->setCurrentIndex(1);
                renderDotGraph(dotContent);
            }
        }
    }

    void CFGVisualizerWindow::loadDotFile() {
        QString filePath = QFileDialog::getOpenFileName(/*...*/);
        if (filePath.isEmpty()) return;

        // Add to current files list if not already present
        if (!currentFiles.contains(filePath)) {
            currentFiles.append(filePath);
            updateFileList();
        }
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Error", "Failed to open DOT file.");
            return;
        }
        
        QTextStream in(&file);
        QString dotContent = in.readAll();
        file.close();
        
        if (outputConsole) {
            outputConsole->clear();
            outputConsole->append("Loaded DOT file: " + filePath);
        }
        
        if (tabWidget) {
            tabWidget->setCurrentIndex(1);
        }
        
        renderDotGraph(dotContent);
    }

    void CFGVisualizerWindow::browseFile() {
        QString filePath = QFileDialog::getOpenFileName(this, "Open File", "", "All Files (*)");
        if (!filePath.isEmpty()) {
            filePathEdit->setText(filePath);
            
            // Auto-detect if it's a DOT file and process accordingly
            if (filePath.endsWith(".dot", Qt::CaseInsensitive) || filePath.endsWith(".gv", Qt::CaseInsensitive)) {
                QFile file(filePath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&file);
                    QString dotContent = in.readAll();
                    file.close();
                    
                    outputConsole->clear();
                    outputConsole->append("Loaded DOT file: " + filePath);
                    tabWidget->setCurrentIndex(1);
                    renderDotGraph(dotContent);
                }
            }
        }
    }

    void CFGVisualizerWindow::clearLoadedFiles() {
        currentFiles.clear();
        if (loadedFilesList) {
            loadedFilesList->clear();
        }
        if (scene) {
            scene->clear();
        }
        statusBar()->showMessage("Cleared all loaded files", 3000);
    }

    void CFGVisualizerWindow::removeSelectedFile() {
        if (!loadedFilesList || loadedFilesList->selectedItems().isEmpty()) {
            QMessageBox::warning(this, "Warning", "No file selected to remove");
            return;
        }
        
        QListWidgetItem* item = loadedFilesList->takeItem(loadedFilesList->currentRow());
        delete item;
        statusBar()->showMessage("Removed selected CFG", 3000);
    }

    void CFGVisualizerWindow::showFileInfo() {
        if (!loadedFilesList || loadedFilesList->selectedItems().isEmpty()) {
            QMessageBox::warning(this, "Warning", "No file selected");
            return;
        }
        
        QString filePath = loadedFilesList->currentItem()->text();
        QFileInfo info(filePath);
        
        QString message = QString(
            "File Information:\n\n"
            "Name: %1\n"
            "Path: %2\n"
            "Size: %3 KB\n"
            "Last Modified: %4"
        ).arg(info.fileName())
        .arg(info.path())
        .arg(info.size() / 1024)
        .arg(info.lastModified().toString());
        
        QMessageBox::information(this, "File Info", message);
    }

    void CFGVisualizerWindow::toggleNodeLabels(bool visible) {
        QList<QGraphicsItem*> items = scene->items();
        for (QGraphicsItem* item : items) {
            if (dynamic_cast<QGraphicsTextItem*>(item)) {
                item->setVisible(visible);
            }
        }
    }

    void CFGVisualizerWindow::toggleEdgeLabels(bool visible) {
        // Assuming you have edge label items in your scene
        QList<QGraphicsItem*> items = scene->items();
        for (QGraphicsItem* item : items) {
            if (item->data(0) == "edge_label") {  // Set this when creating edge labels
                item->setVisible(visible);
            }
        }
    }

    void CFGVisualizerWindow::setGraphTheme(int themeIndex) {
        QColor nodeColor, edgeColor, bgColor;
        
        switch (themeIndex) {
            case 0:  // Light
                nodeColor = QColor(173, 216, 230);  // Light blue
                edgeColor = Qt::darkGray;
                bgColor = Qt::white;
                break;
            case 1:  // Dark
                nodeColor = QColor(70, 130, 180);   // Steel blue
                edgeColor = Qt::lightGray;
                bgColor = QColor(50, 50, 50);
                break;
            case 2:  // High Contrast
                nodeColor = Qt::yellow;
                edgeColor = Qt::red;
                bgColor = Qt::black;
                break;
            default:
                return;
        }
        
        // Apply to all nodes and edges
        QList<QGraphicsItem*> items = scene->items();
        for (QGraphicsItem* item : items) {
            if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
                ellipse->setBrush(nodeColor);
            } else if (auto line = dynamic_cast<QGraphicsLineItem*>(item)) {
                line->setPen(QPen(edgeColor, 1.5));
            }
        }
        
        scene->setBackgroundBrush(bgColor);
    }

    void CFGVisualizerWindow::analyzeFile() {
        QString filePath = filePathEdit->text().trimmed();
        if (filePath.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please select a source file first.");
            return;
        }
    
        outputConsole->clear();
        outputConsole->append("Parsing file: " + filePath);
    
        Parser parser;
        std::vector<Parser::FunctionInfo> functions;  // Add this declaration
        
        if (parser.isDotFile(filePath.toStdString())) {
            functions = parser.extractFunctionsFromDot(filePath.toStdString());
            outputConsole->append("Processing DOT file format");
        } else {
            functions = parser.extractFunctions(filePath.toStdString());
            outputConsole->append("Processing source code file");
        }
    
        // Step 2: Generate CFG
        std::vector<std::unique_ptr<GraphGenerator::CFGGraph>> cfgGraphs;
        
        // Get AST context (ensure parseFile() is public in Parser)
        clang::ASTContext* astContext = parser.parseFile(filePath.toStdString());
        if (!astContext) {
            outputConsole->append("Failed to parse file");
            return;
        }
    
        // Process each function
        for (const auto& functionInfo : functions) {
            auto cfg = GraphGenerator::generateCFG(functionInfo, astContext);
            if (cfg) {
                cfgGraphs.push_back(std::move(cfg));
            }
        }
    
        if (cfgGraphs.empty()) {
            outputConsole->append("CFG generation failed - no valid CFGs created.");
            return;
        }
    
        // Step 3: Visualize CFG
        outputConsole->append("Visualizing CFG...");
        std::string dotGraph = Visualizer::generateDotRepresentation(cfgGraphs[0].get());
        renderDotGraph(QString::fromStdString(dotGraph));
    }

    void CFGVisualizerWindow::loadFunctionDependencies(
            const std::unordered_map<std::string, std::set<std::string>>& dependencies) {
        functionDependencies = dependencies;
        outputConsole->append("Loaded " + QString::number(dependencies.size()) + " function dependencies.");
        renderDependencyGraph();
    }

    void CFGVisualizerWindow::renderDependencyGraph() {
        clearGraph();
        
        // Choose layout algorithm based on current selection
        switch (currentLayoutAlgorithm) {
            case 0: applyForceDirectedLayout(); break;
            case 1: applyHierarchicalLayout(); break;
            case 2: applyCircularLayout(); break;
            default: applyForceDirectedLayout(); break;
        }
        
        // Adjust scene rect and center view
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-50, -50, 50, 50));
        view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
        view->centerOn(0, 0);
    }

    void CFGVisualizerWindow::renderDotGraph(const QString& dotGraph) {
        QProcess process;
        process.start("dot", QStringList() << "-Tplain");
        if (!process.waitForStarted()) {
            outputConsole->append("Failed to start dot process");
            return;
        }
        
        process.write(dotGraph.toUtf8());
        process.closeWriteChannel();
        
        if (!process.waitForFinished()) {
            outputConsole->append("Dot process failed");
            return;
        }

        QString output = QString::fromUtf8(process.readAllStandardOutput());
        parsePlainFormat(output);
    }

    void CFGVisualizerWindow::clearGraph() {
        scene->clear();
    }

    void CFGVisualizerWindow::parsePlainFormat(const QString& plainOutput) {
        scene->clear();  // Clear previous graph

        QStringList lines = plainOutput.split('\n', Qt::SkipEmptyParts);
        
        QMap<QString, QGraphicsEllipseItem*> nodes;

        for (const QString& line : lines) {
            QStringList parts = line.split(' ');
            
            if (parts[0] == "node" && parts.size() >= 7) {
                QString nodeId = parts[1];
                double x = parts[2].toDouble() * 100;  // Scale for better visibility
                double y = parts[3].toDouble() * 100;
                QString label = parts[6];

                QGraphicsEllipseItem* nodeItem = scene->addEllipse(x, y, 40, 40, QPen(Qt::black), QBrush(Qt::lightGray));
                QGraphicsTextItem* textItem = scene->addText(label);
                textItem->setPos(x + 10, y + 10);

                nodes[nodeId] = nodeItem;
            } 
            else if (parts[0] == "edge" && parts.size() >= 3) {
                QString fromNode = parts[1];
                QString toNode = parts[2];

                if (nodes.contains(fromNode) && nodes.contains(toNode)) {
                    QGraphicsEllipseItem* fromItem = nodes[fromNode];
                    QGraphicsEllipseItem* toItem = nodes[toNode];

                    QGraphicsLineItem* edge = scene->addLine(fromItem->rect().center().x(),
                                                            fromItem->rect().center().y(),
                                                            toItem->rect().center().x(),
                                                            toItem->rect().center().y(),
                                                            QPen(Qt::black));
                }
            }
        }
    }

    void CFGVisualizerWindow::applyForceDirectedLayout() {
        // Simple force-directed layout algorithm
        struct NodeInfo {
            QGraphicsEllipseItem* node;
            QGraphicsTextItem* label;
            qreal x, y;
            qreal dx, dy;
        };
        
        // Create nodes for all functions
        std::unordered_map<std::string, NodeInfo> nodes;
        
        // First pass: create all nodes at random positions
        for (const auto& [func, _] : functionDependencies) {
            // Generate random position
            qreal x = QRandomGenerator::global()->bounded(500) - 250;
            qreal y = QRandomGenerator::global()->bounded(500) - 250;
            
            // Create node
            auto* nodeItem = scene->addEllipse(-40, -20, 80, 40, 
                                            QPen(Qt::black), 
                                            QBrush(QColor(100, 149, 237)));
            nodeItem->setPos(x, y);
            nodeItem->setZValue(1);
            
            // Create label
            auto* textItem = scene->addText(QString::fromStdString(func));
            textItem->setPos(x - textItem->boundingRect().width()/2, y - 10);
            textItem->setZValue(2);
            
            // Store node info
            nodes[func] = {nodeItem, textItem, x, y, 0, 0};
        }
        
        // Create edges
        std::vector<QGraphicsLineItem*> edges;
        for (const auto& [caller, callees] : functionDependencies) {
            if (nodes.find(caller) != nodes.end()) {
                for (const auto& callee : callees) {
                    if (nodes.find(callee) != nodes.end()) {
                        // Create a line from caller to callee
                        auto* line = scene->addLine(
                            nodes[caller].x, nodes[caller].y,
                            nodes[callee].x, nodes[callee].y,
                            QPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
                        );
                        line->setZValue(0);
                        edges.push_back(line);
                    }
                }
            }
        }
        
        // Simulate force-directed layout for a few iterations
        const int ITERATIONS = 50;
        const qreal REPULSION = 6000.0;
        const qreal ATTRACTION = 0.06;
        const qreal MAX_DISPLACEMENT = 30.0;
        
        for (int iter = 0; iter < ITERATIONS; iter++) {
            // Calculate repulsive forces between all nodes
            for (auto& [name1, node1] : nodes) {
                node1.dx = 0;
                node1.dy = 0;
                
                for (const auto& [name2, node2] : nodes) {
                    if (name1 == name2) continue;
                    
                    qreal dx = node1.x - node2.x;
                    qreal dy = node1.y - node2.y;
                    qreal distance = std::max(1.0, std::sqrt(dx*dx + dy*dy));
                    
                    // Repulsive force
                    node1.dx += (dx / distance) * (REPULSION / distance);
                    node1.dy += (dy / distance) * (REPULSION / distance);
                }
            }
            
            // Calculate attractive forces along edges
            for (const auto& [caller, callees] : functionDependencies) {
                if (nodes.find(caller) == nodes.end()) continue;
                
                for (const auto& callee : callees) {
                    if (nodes.find(callee) == nodes.end()) continue;
                    
                    qreal dx = nodes[caller].x - nodes[callee].x;
                    qreal dy = nodes[caller].y - nodes[callee].y;
                    qreal distance = std::sqrt(dx*dx + dy*dy);
                    
                    // Attractive force
                    nodes[caller].dx -= dx * ATTRACTION;
                    nodes[caller].dy -= dy * ATTRACTION;
                    nodes[callee].dx += dx * ATTRACTION;
                    nodes[callee].dy += dy * ATTRACTION;
                }
            }
            
            // Apply forces with limit
            for (auto& [name, node] : nodes) {
                qreal displacement = std::sqrt(node.dx*node.dx + node.dy*node.dy);
                if (displacement > 0) {
                    qreal scale = std::min(MAX_DISPLACEMENT, displacement) / displacement;
                    node.x += node.dx * scale;
                    node.y += node.dy * scale;
                }
            }
        }
        
        // Update positions
        for (auto& [name, node] : nodes) {
            node.node->setPos(node.x, node.y);
            node.label->setPos(node.x - node.label->boundingRect().width()/2, 
                            node.y - node.label->boundingRect().height()/2);
        }
        
        // Update edges
        int edgeIndex = 0;
        for (const auto& [caller, callees] : functionDependencies) {
            if (nodes.find(caller) == nodes.end()) continue;
            
            for (const auto& callee : callees) {
                if (nodes.find(callee) == nodes.end() || edgeIndex >= edges.size()) continue;
                
                edges[edgeIndex]->setLine(
                    nodes[caller].x, nodes[caller].y,
                    nodes[callee].x, nodes[callee].y
                );
                edgeIndex++;
            }
        }
    }

    void CFGVisualizerWindow::applyHierarchicalLayout() {
        // Simple hierarchical layout (top to bottom)
        std::unordered_map<std::string, int> depths;
        std::unordered_map<std::string, QGraphicsEllipseItem*> nodeItems;
        std::unordered_map<std::string, QGraphicsTextItem*> textItems;
        
        // Calculate depths (longest path from root)
        std::function<int(const std::string&, std::set<std::string>&)> calculateDepth;
        calculateDepth = [&](const std::string& func, std::set<std::string>& visited) -> int {
            if (visited.find(func) != visited.end()) return 0; // Prevent cycles
            visited.insert(func);
            
            int maxChildDepth = 0;
            if (functionDependencies.find(func) != functionDependencies.end()) {
                for (const auto& callee : functionDependencies.at(func)) {
                    maxChildDepth = std::max(maxChildDepth, calculateDepth(callee, visited));
                }
            }
            
            depths[func] = maxChildDepth + 1;
            return depths[func];
        };
        
        // Find root nodes (not called by anyone)
        std::set<std::string> allFunctions;
        std::set<std::string> calledFunctions;
        
        for (const auto& [caller, callees] : functionDependencies) {
            allFunctions.insert(caller);
            for (const auto& callee : callees) {
                allFunctions.insert(callee);
                calledFunctions.insert(callee);
            }
        }
        
        std::vector<std::string> roots;
        for (const auto& func : allFunctions) {
            if (calledFunctions.find(func) == calledFunctions.end()) {
                roots.push_back(func);
            }
        }
        
        // If no roots found, use all functions as potential roots
        if (roots.empty()) {
            for (const auto& [func, _] : functionDependencies) {
                roots.push_back(func);
            }
        }
        
        // Calculate depths starting from roots
        for (const auto& root : roots) {
            std::set<std::string> visited;
            calculateDepth(root, visited);
        }
        
        // Count functions at each depth
        std::map<int, int> depthCounts;
        for (const auto& [func, depth] : depths) {
            depthCounts[depth]++;
        }
        
        // Position nodes by depth
        const int LEVEL_HEIGHT = 100;
        const int NODE_WIDTH = 120;
        
        for (const auto& [func, depth] : depths) {
            int count = depthCounts[depth];
            int position = 0;
            
            // Find position of this function among functions at the same depth
            int i = 0;
            for (const auto& [otherFunc, otherDepth] : depths) {
                if (otherDepth == depth) {
                    if (otherFunc == func) break;
                    i++;
                }
            }
            position = i;
            
            // Calculate x position to center nodes at each level
            qreal x = (position - (count - 1) / 2.0) * NODE_WIDTH;
            qreal y = depth * LEVEL_HEIGHT;
            
            // Create node
            auto* nodeItem = scene->addEllipse(-40, -20, 80, 40, 
                                            QPen(Qt::black), 
                                            QBrush(QColor(100, 149, 237)));
            nodeItem->setPos(x, y);
            
            // Create label
            auto* textItem = scene->addText(QString::fromStdString(func));
            textItem->setPos(x - textItem->boundingRect().width()/2, y - 10);
            
            nodeItems[func] = nodeItem;
            textItems[func] = textItem;
        }
        
        // Create edges
        for (const auto& [caller, callees] : functionDependencies) {
            if (nodeItems.find(caller) == nodeItems.end()) continue;
            
            for (const auto& callee : callees) {
                if (nodeItems.find(callee) == nodeItems.end()) continue;
                
                QPointF callerPos = nodeItems[caller]->pos();
                QPointF calleePos = nodeItems[callee]->pos();
                
                // Create a line from caller to callee
                scene->addLine(
                    callerPos.x(), callerPos.y() + 20, // Bottom of caller
                    calleePos.x(), calleePos.y() - 20, // Top of callee
                    QPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
                );
            }
        }
    }

    void CFGVisualizerWindow::applyCircularLayout() {
        // Simple circular layout
        std::unordered_map<std::string, QGraphicsEllipseItem*> nodeItems;
        std::unordered_map<std::string, QGraphicsTextItem*> textItems;
        
        // Count total functions
        std::set<std::string> allFunctions;
        for (const auto& [caller, callees] : functionDependencies) {
            allFunctions.insert(caller);
            for (const auto& callee : callees) {
                allFunctions.insert(callee);
            }
        }
        
        int count = allFunctions.size();
        if (count == 0) return;
        
        const qreal RADIUS = 200;
        const qreal CENTER_X = 0;
        const qreal CENTER_Y = 0;
        
        // Position nodes in a circle
        int i = 0;
        for (const auto& func : allFunctions) {
            qreal angle = 2.0 * M_PI * i / count;
            qreal x = CENTER_X + RADIUS * std::cos(angle);
            qreal y = CENTER_Y + RADIUS * std::sin(angle);
            
            // Create node
            auto* nodeItem = scene->addEllipse(-40, -20, 80, 40, 
                                            QPen(Qt::black), 
                                            QBrush(QColor(100, 149, 237)));
            nodeItem->setPos(x, y);
            
            // Create label
            auto* textItem = scene->addText(QString::fromStdString(func));
            textItem->setPos(x - textItem->boundingRect().width()/2, y - 10);
            
            nodeItems[func] = nodeItem;
            textItems[func] = textItem;
            
            i++;
        }
        
        // Create edges
        for (const auto& [caller, callees] : functionDependencies) {
            if (nodeItems.find(caller) == nodeItems.end()) continue;
            
            for (const auto& callee : callees) {
                if (nodeItems.find(callee) == nodeItems.end()) continue;
                
                QPointF callerPos = nodeItems[caller]->pos();
                QPointF calleePos = nodeItems[callee]->pos();
                
                // Create a curved line from caller to callee
                QPointF controlPoint = QPointF(
                    (callerPos.x() + calleePos.x()) / 2 + (calleePos.y() - callerPos.y()) / 4,
                    (callerPos.y() + calleePos.y()) / 2 - (calleePos.x() - callerPos.x()) / 4
                );
                
                QPainterPath path;
                path.moveTo(callerPos);
                path.quadTo(controlPoint, calleePos);
                
                scene->addPath(path, QPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            }
        }
    }

    void CFGVisualizerWindow::zoomIn() {
        zoomFactor *= 1.2;
        view->scale(1.2, 1.2);
    }

    void CFGVisualizerWindow::zoomOut() {
        zoomFactor /= 1.2;
        view->scale(1/1.2, 1/1.2);
    }

    void CFGVisualizerWindow::resetZoom() {
        view->resetTransform();
        zoomFactor = 1.0;
        view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
    }

    void CFGVisualizerWindow::exportGraph() {
        QString fileName = QFileDialog::getSaveFileName(this, "Export Graph", 
                                                    "", "PNG Images (*.png);;PDF Files (*.pdf)");
        if (fileName.isEmpty()) return;
        
        QImage image(scene->sceneRect().size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene->render(&painter);
        
        if (fileName.endsWith(".png", Qt::CaseInsensitive)) {
            if (image.save(fileName)) {
                statusBar()->showMessage("Graph exported to " + fileName, 3000);
            } else {
                QMessageBox::warning(this, "Export Failed", "Failed to export graph to " + fileName);
            }
        } else if (fileName.endsWith(".pdf", Qt::CaseInsensitive)) {
            QPrinter printer(QPrinter::HighResolution);
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOutputFileName(fileName);
            
            QPainter pdfPainter(&printer);
            pdfPainter.setRenderHint(QPainter::Antialiasing);
            scene->render(&pdfPainter);
            
            statusBar()->showMessage("Graph exported to " + fileName, 3000);
        }
    }

    void CFGVisualizerWindow::switchLayoutAlgorithm(int index) {
        currentLayoutAlgorithm = index;
        renderDependencyGraph();
    }

    void CFGVisualizerWindow::showAbout() {
        QMessageBox::about(this, "About CFG Analyzer",
                        "CFG Analyzer - Control Flow Graph Analysis Tool\n\n"
                        "Version 1.0\n\n"
                        "A tool for analyzing and visualizing control flow graphs\n"
                        "and function dependencies in C/C++ code.");
    }

} // namespace CFGAnalyzer