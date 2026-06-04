//===--- BSPGlobalCompilationDatabase.cpp ------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BSPGlobalCompilationDatabase.h"

namespace clang {
namespace clangd {

class BSPProjectModules : public ProjectModules {
public:
  BSPProjectModules(std::shared_ptr<BSPClient> Client)
      : Client(std::move(Client)) {}

  std::vector<std::string> getRequiredModules(PathRef File) override {
    return Client->getRequiredModules(File);
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
  std::shared_ptr<BSPClient> Client;
  CommandMangler Mangler;
};

BSPGlobalCompilationDatabase::BSPGlobalCompilationDatabase(Path BuildServer)
    : Client(std::make_shared<BSPClient>(BuildServer)) {
  Client->startWorker();
}

BSPGlobalCompilationDatabase::~BSPGlobalCompilationDatabase() = default;

std::optional<tooling::CompileCommand>
BSPGlobalCompilationDatabase::getCompileCommand(PathRef File) const {
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
  return std::make_unique<BSPProjectModules>(Client);
}

} // namespace clangd
} // namespace clang
