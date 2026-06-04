//===--- BSPClient.h - BSP client --------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H

#include "LSPBinder.h"
#include "Transport.h"
#include "support/Path.h"
#include "llvm/Support/Program.h"

namespace clang {
namespace clangd {

// Container for BSP server capabilities
class BSPServer {};

/// This class integrates with build system capabilities via Build Server
/// Protocol.
///
class BSPClient : private LSPBinder::RawOutgoing,
                  public std::enable_shared_from_this<BSPClient> {
public:
  BSPClient(Path BuildServer);
  ~BSPClient();

  BSPClient(const BSPClient &other) = delete;
  BSPClient &operator=(const BSPClient &other) = delete;

  // Create a worker thread and setup the server
  void startWorker();
  void sendExit();

  std::vector<std::string> getRequiredModules(PathRef File);

private:
  // Create the child process running the BSP Server that this client connects
  // to
  void launchServer();
  // Send the initialize request which will be the first response handled in the
  // run loop
  void sendInitialize();
  // Run the client loop to continuously monitor for responses from the server
  void run();

private:
  void onInitialize(llvm::Expected<llvm::json::Value> Result);

private:
  // Manage lifetime of the child process that is the BSP Server
  Path BuildServer;
  llvm::sys::ProcessInfo PI;
  llvm::sys::pipe_t ServerStdInPipe;
  llvm::sys::pipe_t ServerStdOutPipe;
  llvm::sys::pipe_t ServerStdErrPipe;
  FILE *ServerStdOutFile;
  std::unique_ptr<llvm::raw_fd_ostream> ServerStdInStream;

  // Most code should not deal with Transport, callMethod, notify directly.
  // Use LSPBinder to handle incoming and outgoing calls.
  std::unique_ptr<clangd::Transport> Transport;
  class MessageHandler;
  std::unique_ptr<MessageHandler> MsgHandler;
  std::mutex TransportWriter;

  void callMethod(StringRef Method, llvm::json::Value Params,
                  Callback<llvm::json::Value> CB) override;
  void notify(StringRef Method, llvm::json::Value Params) override;

  LSPBinder::RawHandlers Handlers;

  // The BSPServer is created after the "initialize" BSP method.
  std::optional<BSPServer> Server;
};
} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_BSPCLIENT_H
