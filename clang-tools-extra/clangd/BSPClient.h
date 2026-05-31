//===--- BSPClient.h - BSP client --------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H

#include "Transport.h"
#include "support/Path.h"
#include "llvm/Support/Program.h"

namespace clang {
namespace clangd {

/// This class integrates with build system capabilities via Build Server
/// Protocol.
///
class BSPClient {
public:
  BSPClient(Path BuildServer);
  ~BSPClient();

  BSPClient(const BSPClient &other) = delete;
  BSPClient &operator=(const BSPClient &other) = delete;

  /// Run BSP client loop, communicating with the Transport provided in the
  /// constructor. This method must not be executed more than once.
  ///
  /// \return Whether we shut down cleanly with a 'shutdown' -> 'exit' sequence.
  bool run();

private:
  void launchServer();

  Path BuildServer;
  llvm::sys::ProcessInfo PI;

  std::unique_ptr<llvm::raw_fd_ostream> ServerStdInStream;

  // Most code should not deal with Transport, callMethod, notify directly.
  // Use LSPBinder to handle incoming and outgoing calls.
  std::unique_ptr<clangd::Transport> Transport;
  class MessageHandler;
  std::unique_ptr<MessageHandler> MsgHandler;
};
} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H
