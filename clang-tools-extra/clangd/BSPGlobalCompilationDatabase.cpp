//===--- BSPGlobalCompilationDatabase.cpp ------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BSPGlobalCompilationDatabase.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/TargetParser/Host.h"

namespace clang {
namespace clangd {

BSPModulesBuilder::BSPModulesBuilder() {}

BSPModulesBuilder::~BSPModulesBuilder() {}

std::unique_ptr<PrerequisiteModules>
BSPModulesBuilder::buildPrerequisiteModulesFor(PathRef File,
                                               const ThreadsafeFS &TFS) {
  return nullptr;
}

bool BSPModulesBuilder::hasRequiredModules(PathRef File) {
  // HACK: For now pretend all files have modules, this check should not be
  // required or should scan....
  return true;
}

class BSPProjectModules : public ProjectModules {
public:
  BSPProjectModules(const BSPGlobalCompilationDatabase &BSPDB) : BSPDB(BSPDB) {}

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
  const BSPGlobalCompilationDatabase &BSPDB;
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
    std::string_view PrimaryOutput = "";
    if (!OperationInfo->DeclaredOutput.empty()) {
      PrimaryOutput = OperationInfo->DeclaredOutput[0];
    }
    auto Command =
        tooling::CompileCommand(OperationInfo->WorkingDirectory, File,
                                std::move(Arguments), PrimaryOutput);

    // FS used for expanding response files.
    // FIXME: ExpandResponseFiles appears not to provide the usual
    // thread-safety guarantees, as the access to FS is not locked!
    // For now, use the real FS, which is known to be threadsafe (if we don't
    // use/change working directory, which ExpandResponseFiles doesn't).
    auto FS = llvm::vfs::getRealFileSystem();
    auto Tokenizer = llvm::Triple(llvm::sys::getProcessTriple()).isOSWindows()
                         ? llvm::cl::TokenizeWindowsCommandLine
                         : llvm::cl::TokenizeGNUCommandLine;
    // Compile command pushed via LSP protocol may have response files that need
    // to be expanded before further processing. For CDB for files it happens in
    // the main CDB when reading it from the JSON file.
    // TODO: Find a better place to do this
    tooling::addExpandedResponseFiles(Command.CommandLine, Command.Directory,
                                      Tokenizer, *FS);

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
  return std::make_unique<BSPProjectModules>(*this);
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
