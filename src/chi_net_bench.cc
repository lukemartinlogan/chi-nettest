//
// Created by llogan on 18/11/24.
//

#include <future>
#include <thread>

#include "hermes_shm/util/config_parse.h"
#include "test_networks/rpc_thallium.h"

namespace chi {

class TestRunner {
 public:
  TestRunner() {
    CHI_THALLIUM->RegisterRpc(
        *CHI_THALLIUM->rpc_pool_, "RpcPing",
        [this](const tl::request &req, const std::string &md) {
          this->RpcPing(req, md);
        });
    CHI_THALLIUM->RegisterRpc(*CHI_THALLIUM->rpc_pool_, "RpcTaskSubmit",
                              [this](const tl::request &req, tl::bulk &bulk,
                                     chi::SegmentedTransfer &xfer) {
                                this->RpcTaskSubmit(req, bulk, xfer);
                              });
  }

  /** The RPC for processing a message with data */
  void RpcTaskSubmit(const tl::request &req, tl::bulk &bulk,
                     chi::SegmentedTransfer &xfer) {
    HILOG(kDebug, "");
    xfer.AllocateBulksServer();
    CHI_THALLIUM->IoCallServerWrite(req, bulk, xfer);
    req.respond((int)0);
  }

  /** An empty ping */
  void RpcPing(const tl::request &req, const std::string &md) {
    HILOG(kDebug, "");
    req.respond((int)0);
  }

  /** Ping every node */
  template<bool ASYNC>
  void PingAll() {
    HILOG(kInfo, "");
    size_t count = 0;
    std::string empty;
    std::vector<thallium::async_response> responses;
    responses.reserve(CHI_RPC->NumHosts());
    for (NodeId node_id = 1; node_id <= CHI_RPC->NumHosts(); node_id++) {
      try {
        if constexpr (ASYNC) {
          auto ret = CHI_THALLIUM->AsyncCall(node_id, "RpcPing", empty);
          responses.emplace_back(std::move(ret));
        } else {
          CHI_THALLIUM->SyncCall<int>(node_id, "RpcPing", empty);
          ++count;
        }
      } catch (tl::margo_exception &err) {
      }
    }
    if constexpr (ASYNC) {
      try {
        for (thallium::async_response &resp : responses) {
          CHI_THALLIUM->Wait<int>(resp);
          ++count;
        }
      } catch (tl::margo_exception &err) {
      }
    }
    HILOG(kInfo, "Pinged {} nodes", count);
  }

  /** I/O Calls */
  template <bool ASYNC>
  void IoCalls(size_t md_size, size_t io_size, size_t rep) {
    HILOG(kInfo, "");
    SegmentedTransfer xfer;
    xfer.md_ = std::string(md_size, 1);
    char *data = (char *)malloc(io_size);
    xfer.bulk_.emplace_back(DataTransfer{DT_WRITE, data, io_size});

    hshm::Timer timer;
    timer.Resume();

    std::vector<thallium::async_response> responses;
    responses.reserve(CHI_RPC->NumHosts() * rep);
    for (NodeId node_id = 1; node_id <= CHI_RPC->NumHosts(); node_id++) {
      for (int i = 0; i < rep; ++i) {
        if constexpr (ASYNC) {
          auto ret = CHI_THALLIUM->AsyncIoCall(node_id, "RpcTaskSubmit", xfer,
                                               DT_WRITE);
          responses.emplace_back(std::move(ret));
        } else {
          CHI_THALLIUM->SyncIoCall<int>(node_id, "RpcTaskSubmit", xfer,
                                         DT_WRITE);
        }
      }
    }
    if constexpr (ASYNC) {
      for (thallium::async_response &resp : responses) {
        CHI_THALLIUM->Wait<int>(resp);
      }
    }
    timer.Pause();
    size_t ops = CHI_RPC->NumHosts() * rep * (io_size + md_size);
    HILOG(kInfo, "(time={}ms, kbps={})", timer.GetMsec(),
          ops / timer.GetMsec());
  }

  /** Metadata calls */
  template <bool ASYNC>
  void MetadataCalls(size_t md_size, int rep) {
    HILOG(kInfo, "");
    std::string md(md_size, 1);
    hshm::Timer timer;
    timer.Resume();
    std::vector<thallium::async_response> responses;
    responses.reserve(CHI_RPC->NumHosts() * rep);
    for (NodeId node_id = 1; node_id <= CHI_RPC->NumHosts(); node_id++) {
      for (int i = 0; i < rep; ++i) {
        if constexpr (ASYNC) {
          auto ret = CHI_THALLIUM->AsyncCall(node_id, "RpcPing", md);
          responses.emplace_back(std::move(ret));
        } else {
          CHI_THALLIUM->SyncCall<int>(node_id, "RpcPing", md);
        }
      }
    }
    if constexpr (ASYNC) {
      for (thallium::async_response &resp : responses) {
        CHI_THALLIUM->Wait<int>(resp);
      }
    }
    timer.Pause();
    size_t ops = CHI_RPC->NumHosts() * rep;
    HILOG(kInfo, "(time={}ms, kops={})", timer.GetMsec(),
          ops / timer.GetMsec());
  }
};

}  // namespace chi

int main(int argc, char **argv) {
  chi::ServerConfig info;
  info.BenchmarkInit(argc, argv);
  CHI_RPC->ServerInit(&info);
  CHI_THALLIUM->ServerInit(CHI_RPC);

  std::this_thread::sleep_for(std::chrono::seconds(info.sleep_));
  HILOG(kInfo, "Done sleeping!");
  chi::TestRunner runner;
  if (CHI_RPC->node_id_ == 1) {
    if (info.test_ == "ping") {
      runner.PingAll<false>();
    } else if (info.test_ == "ping_async") {
      runner.PingAll<true>();
    } else if (info.test_ == "io") {
      runner.IoCalls<false>(info.md_size_, info.io_size_, info.rep_);
    } else if (info.test_ == "io_async") {
      runner.IoCalls<true>(info.md_size_, info.io_size_, info.rep_);
    } else if (info.test_ == "metadata") {
      runner.MetadataCalls<false>(info.md_size_, info.rep_);
    } else if (info.test_ == "metadata_async") {
      runner.MetadataCalls<true>(info.md_size_, info.rep_);
    } else {
      HELOG(kError, "Unknown test: {}", info.test_);
    }
    CHI_THALLIUM->StopAllDaemons();
  } else {
    CHI_THALLIUM->RunDaemon();
  }
}