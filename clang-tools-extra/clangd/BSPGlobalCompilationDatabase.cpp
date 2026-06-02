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
  return {};
}

} // namespace clangd
} // namespace clang
