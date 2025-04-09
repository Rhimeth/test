#include "ast_extractor.h"
#include <fstream>
#include <QJsonDocument>
#include <QFile>

// Include necessary Clang headers
#include <clang/AST/DeclCXX.h>  // For CXXMethodDecl
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>

ASTExtractor::ASTExtractor() {
    // Initialize any required members
}

ASTExtractor::~ASTExtractor() {
    // Clean up if needed
}

void ASTExtractor::extractAST(clang::ASTContext& context, const std::string& outputPath) {
    QJsonObject astJson;
    QJsonArray functionsArray;
    QJsonArray methodsArray;
    QJsonArray variablesArray;

    // Extract translation unit declarations
    for (const auto* decl : context.getTranslationUnitDecl()->decls()) {
        if (!decl->isImplicit()) {
            QJsonObject declJson;
            extractDecl(decl, declJson);
            
            if (!declJson.isEmpty()) {
                if (declJson.contains("isMethod") && declJson["isMethod"].toBool()) {
                    methodsArray.append(declJson);
                } else if (declJson.contains("isFunction")) {
                    functionsArray.append(declJson);
                } else if (declJson.contains("isVariable")) {
                    variablesArray.append(declJson);
                }
            }
        }
    }

    astJson["functions"] = functionsArray;
    astJson["methods"] = methodsArray;
    astJson["variables"] = variablesArray;

    // Write JSON to file
    QFile outFile(QString::fromStdString(outputPath));
    if (outFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(astJson);
        outFile.write(doc.toJson());
        outFile.close();
    } else {
        throw std::runtime_error("Could not open output file: " + outputPath);
    }
}

void ASTExtractor::extractDecl(const clang::Decl* decl, QJsonObject& declJson) {
    if (const auto* funcDecl = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
        if (funcDecl->isThisDeclarationADefinition()) {
            declJson["name"] = QString::fromStdString(funcDecl->getNameAsString());
            declJson["returnType"] = QString::fromStdString(funcDecl->getReturnType().getAsString());
            declJson["isFunction"] = true;
            
            // Extract parameters
            QJsonArray paramsArray;
            for (unsigned i = 0; i < funcDecl->getNumParams(); ++i) {
                const auto* param = funcDecl->getParamDecl(i);
                QJsonObject paramJson;
                paramJson["name"] = QString::fromStdString(param->getNameAsString());
                paramJson["type"] = QString::fromStdString(param->getType().getAsString());
                paramsArray.append(paramJson);
            }
            declJson["parameters"] = paramsArray;

            // Check if it's a method using isa instead of dyn_cast
            if (llvm::isa<clang::CXXMethodDecl>(funcDecl)) {
                declJson["isMethod"] = true;
                declJson["isFunction"] = false;
            }
        }
    }
    else if (const auto* varDecl = llvm::dyn_cast<clang::VarDecl>(decl)) {
        if (!varDecl->isImplicit()) {
            declJson["name"] = QString::fromStdString(varDecl->getNameAsString());
            declJson["type"] = QString::fromStdString(varDecl->getType().getAsString());
            declJson["isVariable"] = true;
        }
    }
}