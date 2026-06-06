//===--- BSPGlobalCompilationDatabase.cpp ------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BSPGlobalCompilationDatabase.h"
#include "clang/Tooling/CompilationDatabase.h"

namespace clang {
namespace clangd {

class BSPProjectModules : public ProjectModules {
public:
  BSPProjectModules(BSPGlobalCompilationDatabase &BSPDB) : BSPDB(BSPDB) {}

  std::vector<std::string> getRequiredModules(PathRef File) override {
    return {};
  }

  std::string getModuleNameForSource(PathRef File) override { return ""; }

  ModuleNameState getModuleNameState(llvm::StringRef ModuleName) override {
    return ModuleNameState::Unknown;
  }

  std::string getSourceForModuleName(llvm::StringRef ModuleName,
                                     PathRef RequiredSourceFile) override {
    return "";
  }

  void setCommandMangler(CommandMangler Mangler) override {
    this->Mangler = std::move(Mangler);
  }

private:
  BSPGlobalCompilationDatabase &BSPDB;
  CommandMangler Mangler;
};

BSPGlobalCompilationDatabase::BSPGlobalCompilationDatabase(Path BuildServer)
    : Client(std::make_shared<BSPClient>(BuildServer)) {
  Client->startWorker();
}

BSPGlobalCompilationDatabase::~BSPGlobalCompilationDatabase() = default;

std::optional<tooling::CompileCommand>
BSPGlobalCompilationDatabase::getCompileCommand(PathRef File) const {
  auto OperationInfo = tryGetOperationInfo(File);
  if (OperationInfo) {
    auto Arguments = std::vector<std::string>(OperationInfo->Arguments);
    Arguments.insert(Arguments.begin(), OperationInfo->Executable);
    auto Command = tooling::CompileCommand(OperationInfo->WorkingDirectory,
                                           File, std::move(Arguments), "");
    return Command;
  }
  return std::nullopt;
}

bool BSPGlobalCompilationDatabase::blockUntilIdle(Deadline Timeout) const {
  return true;
}

std::optional<ProjectInfo>
BSPGlobalCompilationDatabase::getProjectInfo(PathRef File) const {
  return {};
}

std::unique_ptr<ProjectModules>
BSPGlobalCompilationDatabase::getProjectModules(PathRef File) const {
  // TODO: Reevaluate the lifetime of this
  // return std::make_unique<BSPProjectModules>(*this);
  return nullptr;
}

std::optional<OperationInfo>
BSPGlobalCompilationDatabase::tryGetOperationInfo(PathRef File) const {
  const auto OperationRefI = KnownFileOperations.find(File);
  if (OperationRefI == KnownFileOperations.end()) {
    auto LoadOperationInfo = Client->getOperationInfo(File);
    auto InsertResult = KnownFileOperations.insert(
        std::make_pair(File, std::move(LoadOperationInfo)));
    return InsertResult.first->second;
  }
  auto &OperationInfo = OperationRefI->getValue();
  return OperationInfo;
}

} // namespace clangd
} // namespace clang
