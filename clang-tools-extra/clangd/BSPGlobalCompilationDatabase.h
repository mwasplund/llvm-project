//===--- BSPGlobalCompilationDatabase.h --------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPGLOBALCOMPILATIONDATABASE_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPGLOBALCOMPILATIONDATABASE_H

#include "BSPClient.h"
#include "GlobalCompilationDatabase.h"
#include "ProjectModules.h"
#include "support/Path.h"
#include "support/Threading.h"
#include "clang/Tooling/CompilationDatabase.h"
#include <memory>
#include <optional>

namespace clang {
namespace clangd {

class BSPGlobalCompilationDatabase : public GlobalCompilationDatabase {
public:
  BSPGlobalCompilationDatabase(Path BuildServer);
  ~BSPGlobalCompilationDatabase() override;

  std::optional<tooling::CompileCommand>
  getCompileCommand(PathRef File) const override;

  std::optional<ProjectInfo> getProjectInfo(PathRef File) const override;

  std::unique_ptr<ProjectModules>
  getProjectModules(PathRef File) const override;

  bool blockUntilIdle(Deadline Timeout) const override;

private:
  std::shared_ptr<BSPClient> Client;
};

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPGLOBALCOMPILATIONDATABASE_H
