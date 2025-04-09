#include "CFGBridge.h"
#include "cfg_analyzer.h"
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

CFGBridge::CFGBridge(QObject* parent) 
    : QObject(parent), 
      m_process(new QProcess(this)),
      m_analyzer() {
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
    this, &CFGBridge::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &CFGBridge::onProcessError);
}

CFGBridge::~CFGBridge() {
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }
}

CFGAnalyzer::AnalysisResult CFGBridge::analyzeFileSync(const QString& filePath) {
    CFGAnalyzer::AnalysisResult result;
    try {
        // Use the lock methods from CFGAnalyzer
        m_analyzer.lock();
        result = m_analyzer.analyzeFile(filePath);
        m_analyzer.unlock();
    } catch (const std::exception& e) {
        // Ensure mutex is unlocked in case of exception
        if (m_analyzer.tryLock()) {
            m_analyzer.unlock();
        }
        result.success = false;
        result.report = std::string("Analysis failed: ") + e.what();
        emit errorOccurred(QString::fromStdString(e.what()));
    }
    return result;
}

void CFGBridge::analyzeFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        emit errorOccurred("No file specified");
        return;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit errorOccurred("File does not exist: " + filePath);
        return;
    }

    QTemporaryFile dotFile;
    if (!dotFile.open()) {
        emit errorOccurred("Failed to create temporary dot file");
        return;
    }
    
    QTemporaryFile reportFile;
    if (!reportFile.open()) {
        emit errorOccurred("Failed to create temporary report file");
        return;
    }
    
    dotFile.setAutoRemove(false);
    reportFile.setAutoRemove(false);
    m_outputDotFile = dotFile.fileName();
    m_outputReportFile = reportFile.fileName();
    
    dotFile.close();
    reportFile.close();

    QStringList arguments;
    arguments << filePath 
              << "--dot" << m_outputDotFile
              << "--report" << m_outputReportFile;

    m_process->setWorkingDirectory(fileInfo.absolutePath());
    m_process->start("cfg_parser", arguments);

    if (!m_process->waitForStarted()) {
        emit errorOccurred("Failed to start analysis process");
        return;
    }
}

void CFGBridge::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        QString errorMsg = "Analysis process failed: " + m_process->readAllStandardError();
        emit errorOccurred(errorMsg);
        return;
    }

    QFile dotFile(m_outputDotFile);
    if (!dotFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Failed to open DOT file: " + m_outputDotFile);
        return;
    }

    QFile reportFile(m_outputReportFile);
    if (!reportFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit errorOccurred("Failed to open report file: " + m_outputReportFile);
        return;
    }

    QString dotContent = QString::fromUtf8(dotFile.readAll());
    QString reportContent = QString::fromUtf8(reportFile.readAll());

    dotFile.close();
    reportFile.close();

    QFile::remove(m_outputDotFile);
    QFile::remove(m_outputReportFile);

    CFGAnalyzer::AnalysisResult result;
    result.dotOutput = dotContent.toStdString();
    result.report = reportContent.toStdString();
    result.success = true;
    emit analysisComplete(result);
}

void CFGBridge::onProcessError(QProcess::ProcessError error) {
    QString errorMsg;
    switch (error) {
    case QProcess::FailedToStart:
        errorMsg = "Analysis process failed to start. Make sure cfg_parser is installed.";
        break;
    case QProcess::Crashed:
        errorMsg = "Analysis process crashed.";
        break;
    case QProcess::Timedout:
        errorMsg = "Analysis process timed out.";
        break;
    case QProcess::WriteError:
        errorMsg = "Error writing to analysis process.";
        break;
    case QProcess::ReadError:
        errorMsg = "Error reading from analysis process.";
        break;
    default:
        errorMsg = "Unknown error with analysis process.";
        break;
    }

    emit errorOccurred(errorMsg);
}