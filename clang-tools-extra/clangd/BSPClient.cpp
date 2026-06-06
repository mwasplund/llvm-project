//===--- BSPClient.cpp - BSP client ------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BSPClient.h"
#include "support/Cancellation.h"
#include "support/Logger.h"
#include "support/Trace.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include <array>
#include <deque>
#include <fcntl.h>
#include <future>
#include <unistd.h>

namespace clang {
namespace clangd {

namespace {
// Tracks end-to-end latency of high level bsp calls. Measurements are in
// seconds.
constexpr trace::Metric BSPLatency("bsp_latency", trace::Metric::Distribution,
                                   "method_name");
} // namespace

// MessageHandler dispatches incoming BSP messages.
// It handles cross-cutting concerns:
//  - serializes/deserializes protocol objects to JSON
//  - logging of inbound messages
//  - cancellation handling
//  - basic call tracing
// MessageHandler ensures that initialize() is called before any other handler.
class BSPClient::MessageHandler : public Transport::MessageHandler {
public:
  MessageHandler(BSPClient &Client) : Client(Client) {}

  bool onNotify(llvm::StringRef Method, llvm::json::Value Params) override {
    trace::Span Tracer(Method, BSPLatency);
    SPAN_ATTACH(Tracer, "Params", Params);
    // WithContext HandlerContext(handlerContext());
    log("<-- {0}", Method);
    auto Handler = Client.Handlers.NotificationHandlers.find(Method);
    if (Handler != Client.Handlers.NotificationHandlers.end()) {
      Handler->second(std::move(Params));
      // Client.maybeExportMemoryProfile();
      // Client.maybeCleanupMemory();
    } else if (!Client.Server) {
      elog("Notification {0} before initialization", Method);
    } else if (Method == "$/cancelRequest") {
      // onCancel(std::move(Params));
    } else {
      log("unhandled notification {0}", Method);
    }
    return true;
  }

  bool onCall(llvm::StringRef Method, llvm::json::Value Params,
              llvm::json::Value ID) override {
    elog("<-- {0}({1})", Method, ID);
    return false;
  }

  bool onReply(llvm::json::Value ID,
               llvm::Expected<llvm::json::Value> Result) override {
    // WithContext HandlerContext(handlerContext());
    //
    Callback<llvm::json::Value> ReplyHandler = nullptr;
    if (auto IntID = ID.getAsInteger()) {
      std::lock_guard<std::mutex> Mutex(CallMutex);
      // Find a corresponding callback for the request ID;
      for (size_t Index = 0; Index < ReplyCallbacks.size(); ++Index) {
        if (ReplyCallbacks[Index].first == *IntID) {
          ReplyHandler = std::move(ReplyCallbacks[Index].second);
          ReplyCallbacks.erase(ReplyCallbacks.begin() +
                               Index); // remove the entry
          break;
        }
      }
    }

    if (!ReplyHandler) {
      // No callback being found, use a default log callback.
      ReplyHandler = [&ID](llvm::Expected<llvm::json::Value> Result) {
        elog("received a reply with ID {0}, but there was no such call", ID);
        if (!Result)
          llvm::consumeError(Result.takeError());
      };
    }

    // Log and run the reply handler.
    if (Result) {
      log("<-- reply({0})", ID);
      ReplyHandler(std::move(Result));
    } else {
      auto Err = Result.takeError();
      log("<-- reply({0}) error: {1}", ID, Err);
      ReplyHandler(std::move(Err));
    }
    return true;
  }

  // Bind a reply callback to a request. The callback will be invoked when
  // clangd receives the reply from the LSP client.
  // Return a call id of the request.
  llvm::json::Value bindReply(Callback<llvm::json::Value> Reply) {
    std::optional<std::pair<int, Callback<llvm::json::Value>>> OldestCB;
    int ID;
    {
      std::lock_guard<std::mutex> Mutex(CallMutex);
      ID = NextCallID++;
      ReplyCallbacks.emplace_back(ID, std::move(Reply));

      // If the queue overflows, we assume that the client didn't reply the
      // oldest request, and run the corresponding callback which replies an
      // error to the client.
      if (ReplyCallbacks.size() > MaxReplayCallbacks) {
        elog("more than {0} outstanding LSP calls, forgetting about {1}",
             MaxReplayCallbacks, ReplyCallbacks.front().first);
        OldestCB = std::move(ReplyCallbacks.front());
        ReplyCallbacks.pop_front();
      }
    }
    if (OldestCB)
      OldestCB->second(
          error("failed to receive a client reply for request ({0})",
                OldestCB->first));
    return ID;
  }

private:
  // Method calls may be cancelled by ID, so keep track of their state.
  // This needs a mutex: handlers may finish on a different thread, and that's
  // when we clean up entries in the map.
  mutable std::mutex RequestCancelersMutex;
  llvm::StringMap<std::pair<Canceler, /*Cookie*/ unsigned>> RequestCancelers;
  // unsigned NextRequestCookie = 0; // To disambiguate reused IDs, see below.
  void onCancel(const llvm::json::Value &Params) {
    const llvm::json::Value *ID = nullptr;
    if (auto *O = Params.getAsObject())
      ID = O->get("id");
    if (!ID) {
      elog("Bad cancellation request: {0}", Params);
      return;
    }
    auto StrID = llvm::to_string(*ID);
    std::lock_guard<std::mutex> Lock(RequestCancelersMutex);
    auto It = RequestCancelers.find(StrID);
    if (It != RequestCancelers.end())
      It->second.first(); // Invoke the canceler.
  }

  // Context handlerContext() const {
  //   return Context::current().derive(
  //       kCurrentOffsetEncoding,
  //       Server.Opts.Encoding.value_or(OffsetEncoding::UTF16));
  // }

  // We run cancelable requests in a context that does two things:
  //  - allows cancellation using RequestCancelers[ID]
  //  - cleans up the entry in RequestCancelers when it's no longer needed
  // If a client reuses an ID, the last wins and the first cannot be canceled.
  // Context cancelableRequestContext(const llvm::json::Value &ID) {
  //   auto Task = cancelableTask(
  //       /*Reason=*/static_cast<int>(ErrorCode::RequestCancelled));
  //   auto StrID = llvm::to_string(ID);  // JSON-serialize ID for map key.
  //   auto Cookie = NextRequestCookie++; // No lock, only called on main
  //   thread.
  //   {
  //     std::lock_guard<std::mutex> Lock(RequestCancelersMutex);
  //     RequestCancelers[StrID] = {std::move(Task.second), Cookie};
  //   }
  //   // When the request ends, we can clean up the entry we just added.
  //   // The cookie lets us check that it hasn't been overwritten due to ID
  //   // reuse.
  //   return Task.first.derive(llvm::scope_exit([this, StrID, Cookie] {
  //     std::lock_guard<std::mutex> Lock(RequestCancelersMutex);
  //     auto It = RequestCancelers.find(StrID);
  //     if (It != RequestCancelers.end() && It->second.second == Cookie)
  //       RequestCancelers.erase(It);
  //   }));
  // }

  // The maximum number of callbacks held in clangd.
  //
  // We bound the maximum size to the pending map to prevent memory leakage
  // for cases where LSP clients don't reply for the request.
  // This has to go after RequestCancellers and RequestCancellersMutex since it
  // can contain a callback that has a cancelable context.
  static constexpr int MaxReplayCallbacks = 100;
  mutable std::mutex CallMutex;
  int NextCallID = 0; /* GUARDED_BY(CallMutex) */
  std::deque<std::pair</*RequestID*/ int,
                       /*ReplyHandler*/ Callback<llvm::json::Value>>>
      ReplyCallbacks; /* GUARDED_BY(CallMutex) */

  BSPClient &Client;
};

BSPClient::BSPClient(Path BuildServer)
    : BuildServer(BuildServer), Transport(),
      MsgHandler(new MessageHandler(*this)) {}

BSPClient::~BSPClient() {}

void BSPClient::startWorker() {
  auto Task = [self = shared_from_this()]() {
    llvm::set_thread_name("BSPClient");
    self->launchServer();
    self->sendInitialize();
    self->run();
  };

  llvm::thread Thread(
      /*clang::DesiredStackSize*/ std::optional<unsigned>(), std::move(Task));
  Thread.detach();
}

void BSPClient::sendExit() {
  auto ParamsTest = llvm::json::Value(llvm::json::Object{});
  notify("exit", std::move(ParamsTest));
}

std::optional<OperationInfo> BSPClient::getOperationInfo(PathRef File) {
  auto Promise = std::promise<std::optional<OperationInfo>>();
  auto Future = Promise.get_future();
  auto ParamsTest = llvm::json::Value(llvm::json::Object{
      {"file", File},
  });
  Callback<llvm::json::Value> CB =
      [Promise = std::move(Promise)](
          llvm::Expected<llvm::json::Value> Result) mutable {
        std::optional<OperationInfo> OperationInfoResult;

        if (!Result) {
          elog("getOperationInfo failed");
          return;
        }

        auto ResultObject = Result->getAsObject();
        auto FindInfoResult = ResultObject->find("info");
        if (FindInfoResult != ResultObject->end()) {
          const auto &InfoObject = FindInfoResult->second.getAsObject();

          OperationInfo OperationInfo;
          OperationInfo.WorkingDirectory =
              InfoObject->getString("workingDirectory").value();
          OperationInfo.Executable =
              InfoObject->getString("workingDirectory").value();

          for (auto &Value : *InfoObject->getArray("arguments")) {
            OperationInfo.Arguments.push_back(
                std::string(Value.getAsString().value()));
          }

          for (auto &Value : *InfoObject->getArray("declaredInput")) {
            OperationInfo.DeclaredInput.push_back(
                std::string(Value.getAsString().value()));
          }

          for (auto &Value : *InfoObject->getArray("declaredOutput")) {
            OperationInfo.DeclaredOutput.push_back(
                std::string(Value.getAsString().value()));
          }

          OperationInfoResult = std::move(OperationInfo);
        }

        Promise.set_value(std::move(OperationInfoResult));
      };
  callMethod("textDocument/operation/get", std::move(ParamsTest),
             std::move(CB));

  // Wait on the result
  Future.wait();
  return Future.get();
}

void BSPClient::launchServer() {
  // Create a pipe to send stdin to child
  std::array<llvm::sys::pipe_t, 2> StdInPipe;
  if (pipe2(StdInPipe.data(), 0) < 0) {
    elog("Failed to create stdInPipe");
    return;
  }

  // Create a pipe to send stdout to parent
  std::array<llvm::sys::pipe_t, 2> StdOutPipe;
  if (pipe2(StdOutPipe.data(), 0) < 0) {
    elog("Failed to create stdOutPipe");
    return;
  }

  // Create a pipe to send stderr to parent
  std::array<llvm::sys::pipe_t, 2> StdErrPipe;
  if (pipe2(StdErrPipe.data(), 0) < 0) {
    elog("Failed to create stdErrPipe");
    return;
  }

  std::optional<std::array<llvm::sys::pipe_t, 2>> Redirects[] = {
      StdInPipe,
      StdOutPipe,
      StdErrPipe,
  };

  // Spawns the process and moves on immediately
  std::vector<llvm::StringRef> args = {BuildServer};
  PI = llvm::sys::ExecuteNoWait(BuildServer, args, std::nullopt, {}, Redirects);

  // Close our handle that are used for the child
  close(StdInPipe[0]);
  close(StdOutPipe[1]);
  close(StdErrPipe[1]);

  if (PI.Pid != llvm::sys::ProcessInfo::InvalidPid) {
    elog("Launched child process with PID: {0}", PI.Pid);
  }

  ServerStdInPipe = StdInPipe[1];
  ServerStdOutPipe = StdOutPipe[0];
  ServerStdErrPipe = StdErrPipe[0];

  // Get the FILE stream for the read end of the pipe
  ServerStdOutFile = fdopen(ServerStdOutPipe, "r");
  if (ServerStdOutFile == NULL) {
    elog("fdopen");
    return;
  }

  ServerStdInStream =
      std::make_unique<llvm::raw_fd_ostream>(ServerStdInPipe, true);

  Transport = newJSONTransport(ServerStdOutFile, *ServerStdInStream, nullptr,
                               false, JSONStreamStyle::Standard);
}

void BSPClient::sendInitialize() {
  auto ParamsTest = llvm::json::Value(llvm::json::Object{
      {"test", "123"},
  });
  Callback<llvm::json::Value> CB =
      [Self = shared_from_this()](llvm::Expected<llvm::json::Value> Result) {
        Self->onInitialize(std::move(Result));
      };
  callMethod("initialize", std::move(ParamsTest), std::move(CB));
}

void BSPClient::run() {
  // Run the Build Server loop.
  if (auto Err = Transport->loop(*MsgHandler)) {
    elog("Transport error: {0}", std::move(Err));
  }

  PI = llvm::sys::Wait(PI, 10 /*timeout seconds*/);
  if (PI.ReturnCode != 0) {
    elog("BSP server exit not success: {0}", PI.ReturnCode);
  }
}

// call(), notify(), and reply() wrap the Transport, adding logging and locking.
void BSPClient::callMethod(StringRef Method, llvm::json::Value Params,
                           Callback<llvm::json::Value> CB) {
  auto ID = MsgHandler->bindReply(std::move(CB));
  log("--> {0}({1})", Method, ID);
  std::lock_guard<std::mutex> Lock(TransportWriter);
  Transport->call(Method, std::move(Params), ID);
}

void BSPClient::notify(llvm::StringRef Method, llvm::json::Value Params) {
  log("--> {0}", Method);
  // maybeCleanupMemory();
  std::lock_guard<std::mutex> Lock(TransportWriter);
  Transport->notify(Method, std::move(Params));
}

void BSPClient::onInitialize(llvm::Expected<llvm::json::Value> Result) {
  log("test");

  if (Result)
    Server.emplace();
}

} // namespace clangd
} // namespace clang
