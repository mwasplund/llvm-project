//===--- BSPClient.cpp - BSP client ------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BSPClient.h"
#include "support/Logger.h"
#include "support/Trace.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <fcntl.h>
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
    // auto Handler = Server.Handlers.NotificationHandlers.find(Method);
    // if (Handler != Server.Handlers.NotificationHandlers.end()) {
    //   Handler->second(std::move(Params));
    //   Server.maybeExportMemoryProfile();
    //   Server.maybeCleanupMemory();
    // } else if (!Server.Server) {
    //   elog("Notification {0} before initialization", Method);
    // } else if (Method == "$/cancelRequest") {
    //   onCancel(std::move(Params));
    // } else {
    //   log("unhandled notification {0}", Method);
    // }
    return true;
  }

  bool onCall(llvm::StringRef Method, llvm::json::Value Params,
              llvm::json::Value ID) override {
    // WithContext HandlerContext(handlerContext());
    // // Calls can be canceled by the client. Add cancellation context.
    // WithContext WithCancel(cancelableRequestContext(ID));
    trace::Span Tracer(Method, BSPLatency);
    SPAN_ATTACH(Tracer, "Params", Params);
    // ReplyOnce Reply(ID, Method, &Server, Tracer.Args);
    log("<-- {0}({1})", Method, ID);
    // auto Handler = Server.Handlers.MethodHandlers.find(Method);
    // if (Handler != Server.Handlers.MethodHandlers.end()) {
    //   Handler->second(std::move(Params), std::move(Reply));
    // } else if (!Server.Server) {
    //   elog("Call {0} before initialization.", Method);
    //   Reply(llvm::make_error<LSPError>("server not initialized",
    //                                    ErrorCode::ServerNotInitialized));
    // } else {
    //   Reply(llvm::make_error<LSPError>("method not found",
    //                                    ErrorCode::MethodNotFound));
    // }
    return true;
  }

  bool onReply(llvm::json::Value ID,
               llvm::Expected<llvm::json::Value> Result) override {
    // WithContext HandlerContext(handlerContext());
    //
    // Callback<llvm::json::Value> ReplyHandler = nullptr;
    // if (auto IntID = ID.getAsInteger()) {
    //   std::lock_guard<std::mutex> Mutex(CallMutex);
    //   // Find a corresponding callback for the request ID;
    //   for (size_t Index = 0; Index < ReplyCallbacks.size(); ++Index) {
    //     if (ReplyCallbacks[Index].first == *IntID) {
    //       ReplyHandler = std::move(ReplyCallbacks[Index].second);
    //       ReplyCallbacks.erase(ReplyCallbacks.begin() +
    //                            Index); // remove the entry
    //       break;
    //     }
    //   }
    // }
    //
    // if (!ReplyHandler) {
    //   // No callback being found, use a default log callback.
    //   ReplyHandler = [&ID](llvm::Expected<llvm::json::Value> Result) {
    //     elog("received a reply with ID {0}, but there was no such call", ID);
    //     if (!Result)
    //       llvm::consumeError(Result.takeError());
    //   };
    // }
    //
    // // Log and run the reply handler.
    // if (Result) {
    //   log("<-- reply({0})", ID);
    //   ReplyHandler(std::move(Result));
    // } else {
    //   auto Err = Result.takeError();
    //   log("<-- reply({0}) error: {1}", ID, Err);
    //   ReplyHandler(std::move(Err));
    // }
    return true;
  }

  // Bind a reply callback to a request. The callback will be invoked when
  // clangd receives the reply from the LSP client.
  // Return a call id of the request.
  // llvm::json::Value bindReply(Callback<llvm::json::Value> Reply) {
  //   std::optional<std::pair<int, Callback<llvm::json::Value>>> OldestCB;
  //   int ID;
  //   {
  //     std::lock_guard<std::mutex> Mutex(CallMutex);
  //     ID = NextCallID++;
  //     ReplyCallbacks.emplace_back(ID, std::move(Reply));
  //
  //     // If the queue overflows, we assume that the client didn't reply the
  //     // oldest request, and run the corresponding callback which replies an
  //     // error to the client.
  //     if (ReplyCallbacks.size() > MaxReplayCallbacks) {
  //       elog("more than {0} outstanding LSP calls, forgetting about {1}",
  //            MaxReplayCallbacks, ReplyCallbacks.front().first);
  //       OldestCB = std::move(ReplyCallbacks.front());
  //       ReplyCallbacks.pop_front();
  //     }
  //   }
  //   if (OldestCB)
  //     OldestCB->second(
  //         error("failed to receive a client reply for request ({0})",
  //               OldestCB->first));
  //   return ID;
  // }

private:
  // Function object to reply to an LSP call.
  // Each instance must be called exactly once, otherwise:
  //  - the bug is logged, and (in debug mode) an assert will fire
  //  - if there was no reply, an error reply is sent
  //  - if there were multiple replies, only the first is sent
  // class ReplyOnce {
  //   std::atomic<bool> Replied = {false};
  //   std::chrono::steady_clock::time_point Start;
  //   llvm::json::Value ID;
  //   std::string Method;
  //   ClangdLSPServer *Server; // Null when moved-from.
  //   llvm::json::Object *TraceArgs;
  //
  // public:
  //   ReplyOnce(const llvm::json::Value &ID, llvm::StringRef Method,
  //             ClangdLSPServer *Server, llvm::json::Object *TraceArgs)
  //       : Start(std::chrono::steady_clock::now()), ID(ID), Method(Method),
  //         Server(Server), TraceArgs(TraceArgs) {
  //     assert(Server);
  //   }
  //   ReplyOnce(ReplyOnce &&Other)
  //       : Replied(Other.Replied.load()), Start(Other.Start),
  //         ID(std::move(Other.ID)), Method(std::move(Other.Method)),
  //         Server(Other.Server), TraceArgs(Other.TraceArgs) {
  //     Other.Server = nullptr;
  //   }
  //   ReplyOnce &operator=(ReplyOnce &&) = delete;
  //   ReplyOnce(const ReplyOnce &) = delete;
  //   ReplyOnce &operator=(const ReplyOnce &) = delete;
  //
  //   ~ReplyOnce() {
  //     // There's one legitimate reason to never reply to a request: clangd's
  //     // request handler send a call to the client (e.g. applyEdit) and the
  //     // client never replied. In this case, the ReplyOnce is owned by
  //     // ClangdLSPServer's reply callback table and is destroyed along with
  //     the
  //     // server. We don't attempt to send a reply in this case, there's
  //     little
  //     // to be gained from doing so.
  //     if (Server && !Server->IsBeingDestroyed && !Replied) {
  //       elog("No reply to message {0}({1})", Method, ID);
  //       assert(false && "must reply to all calls!");
  //       (*this)(llvm::make_error<LSPError>("server failed to reply",
  //                                          ErrorCode::InternalError));
  //     }
  //   }
  //
  //   void operator()(llvm::Expected<llvm::json::Value> Reply) {
  //     assert(Server && "moved-from!");
  //     if (Replied.exchange(true)) {
  //       elog("Replied twice to message {0}({1})", Method, ID);
  //       assert(false && "must reply to each call only once!");
  //       return;
  //     }
  //     auto Duration = std::chrono::steady_clock::now() - Start;
  //     if (Reply) {
  //       log("--> reply:{0}({1}) {2:ms}", Method, ID, Duration);
  //       if (TraceArgs)
  //         (*TraceArgs)["Reply"] = *Reply;
  //       std::lock_guard<std::mutex> Lock(Server->TranspWriter);
  //       Server->Transp.reply(std::move(ID), std::move(Reply));
  //     } else {
  //       llvm::Error Err = Reply.takeError();
  //       log("--> reply:{0}({1}) {2:ms}, error: {3}", Method, ID, Duration,
  //       Err); if (TraceArgs)
  //         (*TraceArgs)["Error"] = llvm::to_string(Err);
  //       std::lock_guard<std::mutex> Lock(Server->TranspWriter);
  //       Server->Transp.reply(std::move(ID), std::move(Err));
  //     }
  //   }
  // };

  // Method calls may be cancelled by ID, so keep track of their state.
  // This needs a mutex: handlers may finish on a different thread, and that's
  // when we clean up entries in the map.
  // mutable std::mutex RequestCancelersMutex;
  // llvm::StringMap<std::pair<Canceler, /*Cookie*/ unsigned>> RequestCancelers;
  // unsigned NextRequestCookie = 0; // To disambiguate reused IDs, see below.
  // void onCancel(const llvm::json::Value &Params) {
  //   const llvm::json::Value *ID = nullptr;
  //   if (auto *O = Params.getAsObject())
  //     ID = O->get("id");
  //   if (!ID) {
  //     elog("Bad cancellation request: {0}", Params);
  //     return;
  //   }
  //   auto StrID = llvm::to_string(*ID);
  //   std::lock_guard<std::mutex> Lock(RequestCancelersMutex);
  //   auto It = RequestCancelers.find(StrID);
  //   if (It != RequestCancelers.end())
  //     It->second.first(); // Invoke the canceler.
  // }
  //
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
  // static constexpr int MaxReplayCallbacks = 100;
  // mutable std::mutex CallMutex;
  // int NextCallID = 0; /* GUARDED_BY(CallMutex) */
  // std::deque<std::pair</*RequestID*/ int,
  //                      /*ReplyHandler*/ Callback<llvm::json::Value>>>
  //     ReplyCallbacks; /* GUARDED_BY(CallMutex) */

  BSPClient &Client;
};

BSPClient::BSPClient(Path BuildServer)
    : BuildServer(BuildServer), Transport(),
      MsgHandler(new MessageHandler(*this)) {
  launchServer();
  run();
}

BSPClient::~BSPClient() {}

bool BSPClient::run() {
  auto ParamsTest = llvm::json::Value(llvm::json::Object{
      {"test", "123"},
  });
  auto IdTest = llvm::json::Value(123);
  Transport->call("initialize", ParamsTest, IdTest);

  // Run the Build Server loop.
  bool CleanExit = true;
  if (auto Err = Transport->loop(*MsgHandler)) {
    elog("Transport error: {0}", std::move(Err));
    CleanExit = false;
  }

  auto Params = llvm::json::Value(llvm::json::Object{});
  auto Id = llvm::json::Value(123);
  Transport->call("exit", Params, Id);

  PI = llvm::sys::Wait(PI, 10 /*timeout seconds*/);
  if (PI.ReturnCode != 0) {
    elog("BSP server exit not success: {0}", PI.ReturnCode);
  }

  return CleanExit;
}

void BSPClient::launchServer() {
  // Create a pipe to send stdin to child
  std::array<llvm::sys::pipe_t, 2> stdInPipe;
  if (pipe2(stdInPipe.data(), O_NONBLOCK) < 0) {
    elog("Failed to create stdInPipe");
    return;
  }

  // Create a pipe to send stdout to parent
  std::array<llvm::sys::pipe_t, 2> stdOutPipe;
  if (pipe2(stdOutPipe.data(), O_NONBLOCK) < 0) {
    elog("Failed to create stdOutPipe");
    return;
  }

  // Create a pipe to send stderr to parent
  std::array<llvm::sys::pipe_t, 2> stdErrPipe;
  if (pipe2(stdErrPipe.data(), O_NONBLOCK) < 0) {
    elog("Failed to create stdErrPipe");
    return;
  }

  std::optional<std::array<llvm::sys::pipe_t, 2>> Redirects[] = {
      stdInPipe,
      stdOutPipe,
      stdErrPipe,
  };

  // Spawns the process and moves on immediately
  std::vector<llvm::StringRef> args = {BuildServer};
  PI = llvm::sys::ExecuteNoWait(BuildServer, args, std::nullopt, {}, Redirects);

  // Close our handle that are used for the child
  close(stdInPipe[0]);
  close(stdOutPipe[1]);
  close(stdErrPipe[1]);

  if (PI.Pid != llvm::sys::ProcessInfo::InvalidPid) {
    elog("Launched child process with PID: {0}", PI.Pid);
  }

  // Get the FILE stream for the read end of the pipe
  FILE *StdOutFile = fdopen(stdOutPipe[0], "r");
  if (StdOutFile == NULL) {
    elog("fdopen");
    return;
  }

  ServerStdInStream =
      std::make_unique<llvm::raw_fd_ostream>(stdInPipe[1], true);

  Transport = newJSONTransport(StdOutFile, *ServerStdInStream, nullptr, false,
                               JSONStreamStyle::Standard);
}
} // namespace clangd
} // namespace clang
