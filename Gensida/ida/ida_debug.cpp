#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>

#ifdef DEBUG_68K
#include "proto/debug_proto_68k.grpc.pb.h"
#else
#include "proto/debug_proto_z80.grpc.pb.h"
#endif

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using idadebug::DbgServer;
using idadebug::DbgClient;
using idadebug::AnyRegValue;
using idadebug::GpRegsEnum;
using idadebug::GpReg;
using idadebug::GpRegValue;
using idadebug::GpRegs;
#ifdef DEBUG_68K
using idadebug::VdpRegsEnum;
using idadebug::VdpReg;
using idadebug::VdpRegValue;
using idadebug::VdpRegs;
using idadebug::DmaInfo;
#endif
using idadebug::Changed;
using idadebug::Condition;
using idadebug::MemData;
using idadebug::MemoryAS;
using idadebug::MemoryAD;
using idadebug::BpType;
using idadebug::DbgBreakpoint;
using idadebug::DbgBreakpoints;
using idadebug::Callstack;
using idadebug::PauseChanged;
using google::protobuf::Empty;
using google::protobuf::BoolValue;
using google::protobuf::Map;

#include <ida.hpp>
#include <idd.hpp>
#include <dbg.hpp>
#include <auto.hpp>
#include <expr.hpp>

#include "ida_debmod.h"

#include "ida_registers.h"
#include "ida_plugin.h"
#include "ida_debug.h"

typedef qvector<std::pair<uint32, bool>> codemap_t;
static eventlist_t events;

static std::atomic<bool> server_stopping{true};

#ifdef DEBUG_68K
#define BREAKPOINTS_BASE 0x00D00000
#endif

static const char *const SRReg[] = {
#ifdef DEBUG_68K
  "C",
  "V",
  "Z",
  "N",
  "X",
  NULL,
  NULL,
  NULL,
  "I",
  "I",
  "I",
  NULL,
  NULL,
  "S",
  NULL,
  "T"
#else
  "CF",
  "NF",
  "PF",
  "B3",
  "HF",
  "B5",
  "ZF",
  "SF",
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
#endif
};

register_info_t registers[] = {
#ifdef DEBUG_68K
  { "D0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
  { "D7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

  { "A0", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A0_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A1", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A1_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A2", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A2_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A3", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A3_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A4", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A4_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A5", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A5_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A6", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A6_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "A7", 0, RC_GENERAL, dt_dword, NULL, 0 },
  { "A7_ADDR", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },

  { "PC", REGISTER_ADDRESS | REGISTER_IP | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },
  { "SR", 0, RC_GENERAL, dt_word, SRReg, 0xFFFF },

  { "DMA_LEN", REGISTER_READONLY, RC_GENERAL, dt_word, NULL, 0 },
  { "DMA_SRC", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },
  { "VDP_DST", REGISTER_ADDRESS | REGISTER_READONLY, RC_GENERAL, dt_dword, NULL, 0 },

  // VDP Registers
  { "Set1", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Set2", 0, RC_VDP, dt_byte, NULL, 0 },
  { "PlaneA", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Window", 0, RC_VDP, dt_byte, NULL, 0 },
  { "PlaneB", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Sprite", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Reg6", 0, RC_VDP, dt_byte, NULL, 0 },
  { "BgClr", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Reg8", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Reg9", 0, RC_VDP, dt_byte, NULL, 0 },
  { "HInt", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Set3", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Set4", 0, RC_VDP, dt_byte, NULL, 0 },
  { "HScrl", 0, RC_VDP, dt_byte, NULL, 0 },
  { "Reg14", 0, RC_VDP, dt_byte, NULL, 0 },
  { "WrInc", 0, RC_VDP, dt_byte, NULL, 0 },
  { "ScrSz", 0, RC_VDP, dt_byte, NULL, 0 },
  { "WinX", 0, RC_VDP, dt_byte, NULL, 0 },
  { "WinY", 0, RC_VDP, dt_byte, NULL, 0 },
  { "LenLo", 0, RC_VDP, dt_byte, NULL, 0 },
  { "LenHi", 0, RC_VDP, dt_byte, NULL, 0 },
  { "SrcLo", 0, RC_VDP, dt_byte, NULL, 0 },
  { "SrcMid", 0, RC_VDP, dt_byte, NULL, 0 },
  { "SrcHi", 0, RC_VDP, dt_byte, NULL, 0 },
#else
  { "AF", 0, RC_GENERAL, dt_word, SRReg, 0x00FF },
  { "BC", 0, RC_GENERAL, dt_word, NULL, 0 },
  { "DE", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
  { "HL", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

  { "IX", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
  { "IY", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

  { "A", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "B", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "C", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "D", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "E", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "H", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "L", 0, RC_GENERAL, dt_byte, NULL, 0 },

  { "IXH", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "IXL", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "IYH", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "IYL", 0, RC_GENERAL, dt_byte, NULL, 0 },

  { "AF'", 0, RC_GENERAL, dt_word, NULL, 0 },
  { "BC'", 0, RC_GENERAL, dt_word, NULL, 0 },
  { "DE'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },
  { "HL'", REGISTER_ADDRESS, RC_GENERAL, dt_word, NULL, 0 },

  { "I", 0, RC_GENERAL, dt_byte, NULL, 0 },
  { "R", 0, RC_GENERAL, dt_word, NULL, 0 },

  { "SP", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_word, NULL, 0 },
  { "IP", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_word, NULL, 0 },

  { "BANK", 0, RC_GENERAL, dt_dword, NULL, 0 }
#endif
};

static const char *register_classes[] = {
  "General Registers",
#ifdef DEBUG_68K
  "VDP Registers",
#endif
  NULL
};

struct apply_codemap_req : public exec_request_t {
private:
  const Map<google::protobuf::uint32, google::protobuf::uint32>& _changed;
public:
  apply_codemap_req(const google::protobuf::Map<google::protobuf::uint32, google::protobuf::uint32>& changed) : _changed(changed) {};

  ssize_t idaapi execute(void) override {
    for (auto i = _changed.cbegin(); i != _changed.cend(); ++i) {
      auto_make_code((ea_t)i->first);
      plan_ea((ea_t)i->first);
      show_addr((ea_t)i->first);
    }

    return 0;
  }
};

static std::mutex apply_lock;
static google::protobuf::Map<google::protobuf::uint32, google::protobuf::uint32> to_apply;

// Called only from IDA's main-thread stop/termination callbacks.
static void flush_codemap() {
  Map<google::protobuf::uint32, google::protobuf::uint32> pending;
  {
    std::lock_guard<std::mutex> lock(apply_lock);
    pending.swap(to_apply);
  }
  apply_codemap_req req(pending);
  req.execute();
}

static void apply_codemap(const google::protobuf::Map<google::protobuf::uint32, google::protobuf::uint32>& changed) {
  if (changed.empty()) return;

  std::lock_guard<std::mutex> lock(apply_lock);
  for (const auto &entry : changed)
    to_apply[entry.first] = entry.second;
}

void stop_server();

class cond_break_t : public exec_request_t {
    uint32 elang = 0;
    qstring cond;
public:
    cond_break_t() {};
    cond_break_t(uint32 _elang, const char* _cond) : elang(_elang), cond(_cond) {};

    ssize_t idaapi execute(void) override {
        extlang_object_t elng = find_extlang_by_index(elang);
        if (!elng)
            return 1;

        idc_value_t rv;
        qstring errbuf;
        qstring func;

        if (elng->is_idc()) {
            func.sprnt("static main() {\n");
        }
        else {
            func.sprnt("def main():\n");
        }

        qstrvec_t lines;
        qstring full(cond);
        full.split(&lines, "\n");

        for (const auto line : lines) {
            func.cat_sprnt("  %s\n", line.c_str());
        }

        if (elng->is_idc()) {
            func.cat_sprnt("}\n");
        }

        bool res = elng->eval_snippet(func.c_str(), &errbuf);

        if (!res || !errbuf.empty()) {
            warning("%s\n", errbuf.c_str());
        }
        else {
            res = elng->call_func(&rv, "main", nullptr, 0, &errbuf);

            if (!res || !errbuf.empty()) {
                warning("%s\n", errbuf.c_str());
            }
            else {
                return rv.num;
            }
        }
        return 1;
    }
};

struct condition_result_t {
  std::mutex mutex;
  std::condition_variable ready;
  bool done = false;
  ssize_t value = 1;
};

// Self-owned after scheduling, just like IDAPython's MFF_NOWAIT requests.
struct condition_request_t : public exec_request_t {
  cond_break_t condition;
  std::shared_ptr<condition_result_t> result;
  condition_request_t(const Condition &request,
                      std::shared_ptr<condition_result_t> output)
    : condition(request.elang(), request.condition().c_str()), result(output) {}
  ssize_t idaapi execute() override {
    auto output = result;
    ssize_t value = 1;
    if (!server_stopping) {
      suspend_process();
      value = condition.execute();
      continue_process();
    }
    delete this;
    {
      std::lock_guard<std::mutex> lock(output->mutex);
      output->value = value;
      output->done = true;
    }
    output->ready.notify_all();
    return value;
  }
};

class DbgClientHandler final : public DbgClient::Service {
  Status pause_event(ServerContext* context, const PauseChanged* request, Empty* response) override {
    apply_codemap(request->changed());

    debug_event_t ev;
    ev.pid = 1;
    ev.tid = 1;
    ev.ea = request->address();
    ev.handled = true;
    ev.set_eid(PROCESS_SUSPENDED);
    events.enqueue(ev, IN_BACK);

    return Status::OK;
  }

  Status start_event(ServerContext* context, const Empty* request, Empty* response) override {
    debug_event_t ev;
    ev.pid = 1;
    ev.tid = 1;
    ev.ea = BADADDR;
    ev.handled = true;

    ev.set_modinfo(PROCESS_STARTED).name.sprnt("GENS");
    ev.set_modinfo(PROCESS_STARTED).base = 0;
    ev.set_modinfo(PROCESS_STARTED).size = 0;
    ev.set_modinfo(PROCESS_STARTED).rebase_to = BADADDR;

    events.enqueue(ev, IN_BACK);
    return Status::OK;
  }

  Status stop_event(ServerContext* context, const Changed* request, Empty* response) override {
    apply_codemap(request->changed());

    debug_event_t ev;
    ev.pid = 1;
    ev.handled = true;
    ev.set_exit_code(PROCESS_EXITED, 0);
    events.enqueue(ev, IN_BACK);

    return Status::OK;
  }

  Status eval_condition(ServerContext* context, const Condition* request, BoolValue* response) override {
      auto result = std::make_shared<condition_result_t>();
      auto *req = new condition_request_t(*request, result);
      const auto id = execute_sync(*req, MFF_FAST | MFF_NOWAIT);
      if (id < 0) {
        delete req;
        return Status(grpc::StatusCode::CANCELLED, "IDA is shutting down");
      }
      std::unique_lock<std::mutex> lock(result->mutex);
      while (!result->done) {
        if (server_stopping || context->IsCancelled()) {
          lock.unlock();
          if (cancel_exec_request(int(id)))
            delete req;
          return Status(grpc::StatusCode::CANCELLED, "Condition evaluation cancelled");
        }
        result->ready.wait_for(lock, std::chrono::milliseconds(10));
      }
      response->set_value(result->value != 0);
      return Status::OK;
  }
};

namespace {
// gRPC owns its worker threads; no detached thread is needed to call Wait().
// Keep the service alive until all handlers have returned.
class rpc_server_t {
  std::mutex mutex_;
  std::unique_ptr<DbgClientHandler> service_;
  std::unique_ptr<grpc::Server> server_;

  void stop_locked() {
    if (server_) {
      server_->Shutdown(std::chrono::system_clock::now()
                        + std::chrono::milliseconds(100));
      server_->Wait();
      server_.reset();
    }
    service_.reset();
  }

public:
  ~rpc_server_t() { stop(); }

  bool start(int port, int *bound_port = nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_locked();
    if (port < 0 || port > 65535 || (port == 0 && !bound_port))
      return false;
    service_.reset(new DbgClientHandler);
    grpc::ServerBuilder builder;
    builder.AddChannelArgument("grpc.so_reuseport", 0);
    builder.AddListeningPort("127.0.0.1:" + std::to_string(port),
                             grpc::InsecureServerCredentials(), bound_port);
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    if (!server_)
      service_.reset();
    return server_ != nullptr;
  }

  void stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_locked();
  }
};

} // namespace

static rpc_server_t server;

void stop_server() {
  server_stopping = true;
  server.stop();
}

static bool init_ida_server(int portnum, qstring *errbuf) {
  stop_server();
  server_stopping = false;
  if (server.start(portnum))
    return true;
  server_stopping = true;
  if (errbuf)
    errbuf->sprnt("Cannot listen on debugger callback port %d", portnum);
  return false;
}

class DbgServerClient {
public:
  DbgServerClient(std::shared_ptr<Channel> channel)
    : stub_(DbgServer::NewStub(channel)) {}

  bool get_gp_reg(GpRegsEnum index, uint32_t* _return) {
    GpReg reg;
    reg.set_reg(index);

    AnyRegValue value;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_gp_reg(&context, reg, &value);

    *_return = 0;

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    *_return = value.value();
    return true;
  }

  bool get_gp_regs(GpRegs* regs) {
    Empty req;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_gp_regs(&context, req, regs);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool set_gp_reg(GpRegsEnum index, uint32_t value) {
    GpRegValue regVal;
    regVal.set_index(index);
    regVal.set_value(value);

    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->set_gp_reg(&context, regVal, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

#ifdef DEBUG_68K
  bool get_vdp_reg(VdpRegsEnum index, uint8_t* _return) {
    VdpReg reg;
    reg.set_reg(index);

    AnyRegValue value;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_vdp_reg(&context, reg, &value);

    *_return = 0;

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    *_return = value.value() & 0xFF;
    return true;
  }

  bool get_vdp_regs(VdpRegs* regs) {
    Empty req;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_vdp_regs(&context, req, regs);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool set_vdp_reg(VdpRegsEnum index, uint8_t value) {
    VdpRegValue regVal;
    regVal.set_index(index);
    regVal.set_value(value);

    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->set_vdp_reg(&context, regVal, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool get_dma_info(DmaInfo* info) {
    Empty req;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_dma_info(&context, req, info);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }
#endif

  ssize_t read_memory(uint32_t address, uint32_t size, uint8_t* result, qstring *errbuf) {
    MemoryAS req;
    req.set_address(address);
    req.set_size(size);

    MemData data;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->read_memory(&context, req, &data);

    if (!status.ok()) {
      if (errbuf)
        errbuf->sprnt("Memory read failed: %s", status.error_message().c_str());
      return 0;
    }

    const size_t received = data.data().size();
    if (received > size) {
      if (errbuf)
        *errbuf = "Emulator returned more memory bytes than requested";
      return 0;
    }
    if (received != 0)
      data.data().copy(reinterpret_cast<char*>(result), received);
    if (received < size && errbuf)
      *errbuf = "Emulator returned fewer memory bytes than requested";
    return static_cast<ssize_t>(received);
  }

  bool write_memory(const uint8_t* data, uint32_t address, uint32_t size) {
    MemoryAD req;
    req.set_address(address);
    req.set_data(data, size);

    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->write_memory(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool get_breakpoints(std::vector<DbgBreakpoint> _return) {
    Empty req;

    DbgBreakpoints bpts;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_breakpoints(&context, req, &bpts);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    _return.clear();

    for (auto i = bpts.list().cbegin(); i != bpts.list().cend(); ++i) {
      _return.push_back(*i);
    }

    return true;
  }

  bool add_breakpoint(const DbgBreakpoint& bpt) {
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->add_breakpoint(&context, bpt, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool toggle_breakpoint(const DbgBreakpoint& bpt) {
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->toggle_breakpoint(&context, bpt, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool update_breakpoint(const DbgBreakpoint& bpt) {
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->update_breakpoint(&context, bpt, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool del_breakpoint(const DbgBreakpoint& bpt) {
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->del_breakpoint(&context, bpt, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool clear_breakpoints() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->clear_breakpoints(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool pause() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->pause(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool resume() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->resume(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool start() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->start_emulation(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool exit() {
    show_wait_box("HIDECANCEL\nFinishing execution...");

    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(100));
    Status status = stub_->exit_emulation(&context, req, &resp);

    hide_wait_box();

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool step_into() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->step_into(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool step_over() {
    Empty req;
    Empty resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->step_over(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    return true;
  }

  bool get_callstack(std::vector<uint32_t>& _return) {
    Empty req;
    Callstack resp;

    ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    Status status = stub_->get_callstack(&context, req, &resp);

    if (!status.ok()) {
      //warning("%s\n", status.error_message().c_str());
      return false;
    }

    _return.clear();

    for (auto i = resp.callstack().cbegin(); i != resp.callstack().cend(); ++i) {
      _return.push_back(*i);
    }

    return true;
  }

private:
  std::unique_ptr<DbgServer::Stub> stub_;
};

static std::unique_ptr<DbgServerClient> client;

void shutdown_debugger() {
  stop_server();
  client.reset();
  events.close();
  std::lock_guard<std::mutex> lock(apply_lock);
  to_apply.clear();
}

static bool pause_execution() {
  return client && client->pause();
}

static bool continue_execution() {
  return client && client->resume();
}

static bool finish_execution() {
  if (client && !client->exit()) {
    debug_event_t ev;
    ev.pid = 1;
    ev.handled = true;
    ev.set_exit_code(PROCESS_EXITED, 0);

    events.enqueue(ev, IN_BACK);
    return false;
  }

  return true;
}

static bool init_emu_client(int portnum) {
  qstring conn;
  conn.sprnt("localhost:%d", portnum);

  auto channel = grpc::CreateChannel(conn.c_str(), grpc::InsecureChannelCredentials());

  show_wait_box("Waiting for GENS emulation...");

  while (channel->GetState(true) != GRPC_CHANNEL_READY) {
    if (user_cancelled()) {
      hide_wait_box();
      return false;
    }
    channel->WaitForStateChange(channel->GetState(false),
      std::chrono::system_clock::now() + std::chrono::milliseconds(50));
  }

  hide_wait_box();

  client = std::unique_ptr<DbgServerClient>(new DbgServerClient(channel));
  return true;
}

// Initialize debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi init_debugger(const char* hostname, int portnum, const char* password, qstring *errbuf) {
#ifdef DEBUG_68K
  set_processor_type("68020", SETPROC_LOADER); // reset proc to "M68020"
#endif
  if (portnum < 0 || portnum > 64535) {
    if (errbuf)
      *errbuf = "Debugger port must be between 1 and 64535 (0 selects 23946)";
    return DRC_FAILED;
  }
  netnode n;
  n.create("$ portnum");
  n.set_long(portnum == 0 ? 23946 : portnum);
  return DRC_OK;
}

// Terminate debugger
// Returns true-success
// This function is called from the main thread
static drc_t idaapi term_debugger(void) {
  finish_execution();
  stop_server();
  client = nullptr;
  flush_codemap();
  events.close();
  return DRC_OK;
}

// Return information about the n-th "compatible" running process.
// If n is 0, the processes list is reinitialized.
// 1-ok, 0-failed, -1-network error
// This function is called from the main thread
static drc_t s_get_processes(procinfo_vec_t* procs, qstring* errbuf) {
  process_info_t info;
  info.name.sprnt("GENS");
  info.pid = 1;
  procs->add(info);

  return DRC_OK;
}

// Start an executable to debug
// 1 - ok, 0 - failed, -2 - file not found (ask for process options)
// 1|CRC32_MISMATCH - ok, but the input file crc does not match
// -1 - network error
// This function is called from debthread
static drc_t idaapi s_start_process(const char* path,
  const char* args,
  const char* startdir,
  uint32 dbg_proc_flags,
  const char* input_path,
  uint32 input_file_crc32,
  qstring* errbuf = NULL) {
  stop_server();
  client.reset();
  events.reset();
  {
    std::lock_guard<std::mutex> lock(apply_lock);
    to_apply.clear();
  }

  netnode n("$ portnum");
  int port = (int)n.long_value();
  if (port < 1 || port > 64535) {
    if (errbuf)
      *errbuf = "Invalid debugger port; initialize the debugger before starting";
    return DRC_FAILED;
  }
  if (!init_ida_server(port + 1000, errbuf))
    return DRC_NETERR;
  if (!init_emu_client(port) || (client && !client->start())) {
    stop_server();
    client.reset();
    events.close();
    if (errbuf)
      *errbuf = "Emulator connection cancelled or startup RPC failed";
    return DRC_NETERR;
  }

  return DRC_OK;
}

// rebase database if the debugged program has been rebased by the system
// This function is called from the main thread
static void idaapi rebase_if_required_to(ea_t new_base) { }

// Prepare to pause the process
// This function will prepare to pause the process
// Normally the next get_debug_event() will pause the process
// If the process is sleeping then the pause will not occur
// until the process wakes up. The interface should take care of
// this situation.
// If this function is absent, then it won't be possible to pause the program
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi prepare_to_pause_process(qstring *errbuf) {
  return pause_execution() ? DRC_OK : DRC_FAILED;
}

// Stop the process.
// May be called while the process is running or suspended.
// Must terminate the process in any case.
// The kernel will repeatedly call get_debug_event() and until PROCESS_EXIT.
// In this mode, all other events will be automatically handled and process will be resumed.
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi emul_exit_process(qstring *errbuf) {
  finish_execution();
  return DRC_OK;
}

// Get a pending debug event and suspend the process
// This function will be called regularly by IDA.
// This function is called from debthread
static gdecode_t idaapi get_debug_event(debug_event_t *event, int timeout_ms) {
  return events.retrieve(event, timeout_ms);
}

// Continue after handling the event
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi continue_after_event(const debug_event_t *event) {
  dbg_notification_t req = get_running_notification();

  switch (event->eid()) {
  case STEP:
  case PROCESS_SUSPENDED:
    if (req == dbg_null || req == dbg_run_to) {
      return continue_execution() ? DRC_OK : DRC_FAILED;
    }
    break;
  case PROCESS_EXITED:
    stop_server();
    break;
  }

  return DRC_OK;
}

// The following function will be called by the kernel each time
// when it has stopped the debugger process for some reason,
// refreshed the database and the screen.
// The debugger module may add information to the database if it wants.
// The reason for introducing this function is that when an event line
// LOAD_DLL happens, the database does not reflect the memory state yet
// and therefore we can't add information about the dll into the database
// in the get_debug_event() function.
// Only when the kernel has adjusted the database we can do it.
// Example: for imported PE DLLs we will add the exported function
// names to the database.
// This function pointer may be absent, i.e. NULL.
// This function is called from the main thread
static void idaapi stopped_at_debug_event(bool dlls_added) { flush_codemap(); }

// The following functions manipulate threads.
// 1-ok, 0-failed, -1-network error
// These functions are called from debthread
static drc_t idaapi s_set_resume_mode(thid_t tid, resume_mode_t resmod) { // Run one instruction in the thread
  switch (resmod) {
  case RESMOD_INTO:    ///< step into call (the most typical single stepping)
    return client && client->step_into() ? DRC_OK : DRC_FAILED;
  case RESMOD_OVER:    ///< step over call
    return client && client->step_over() ? DRC_OK : DRC_FAILED;
  }

  return DRC_FAILED;
}

// Read thread registers
//	tid	- thread id
//	clsmask- bitmask of register classes to read
//	regval - pointer to vector of regvals for all registers
//			 regval is assumed to have debugger_t::registers_size elements
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi read_registers(thid_t tid, int clsmask, regval_t *values, qstring *errbuf) {
  if (clsmask & RC_GENERAL) {
    GpRegs regs;

    if (client && !client->get_gp_regs(&regs)) {
      return DRC_FAILED;
    }

#ifdef DEBUG_68K
    values[R_D0].ival = regs.d0();
    values[R_D1].ival = regs.d1();
    values[R_D2].ival = regs.d2();
    values[R_D3].ival = regs.d3();
    values[R_D4].ival = regs.d4();
    values[R_D5].ival = regs.d5();
    values[R_D6].ival = regs.d6();
    values[R_D7].ival = regs.d7();

    values[R_A0].ival = regs.a0();
    values[R_A0_ADDR].ival = regs.a0() & 0xFFFFFF;
    values[R_A1].ival = regs.a1();
    values[R_A1_ADDR].ival = regs.a1() & 0xFFFFFF;
    values[R_A2].ival = regs.a2();
    values[R_A2_ADDR].ival = regs.a2() & 0xFFFFFF;
    values[R_A3].ival = regs.a3();
    values[R_A3_ADDR].ival = regs.a3() & 0xFFFFFF;
    values[R_A4].ival = regs.a4();
    values[R_A4_ADDR].ival = regs.a4() & 0xFFFFFF;
    values[R_A5].ival = regs.a5();
    values[R_A5_ADDR].ival = regs.a5() & 0xFFFFFF;
    values[R_A6].ival = regs.a6();
    values[R_A6_ADDR].ival = regs.a6() & 0xFFFFFF;
    values[R_A7].ival = regs.a7();
    values[R_A7_ADDR].ival = regs.a7() & 0xFFFFFF;

    values[R_PC].ival = regs.pc() & 0xFFFFFF;
    values[R_SP].ival = regs.sp() & 0xFFFFFF;
    values[R_SR].ival = regs.sr();
#else
    values[R_AF].ival = regs.af();
    values[R_BC].ival = regs.bc();
    values[R_DE].ival = regs.de();
    values[R_HL].ival = regs.hl();

    values[R_IX].ival = regs.ix();
    values[R_IY].ival = regs.iy();

    values[R_A].ival = regs.a();
    values[R_B].ival = regs.b();
    values[R_C].ival = regs.c();
    values[R_D].ival = regs.d();
    values[R_E].ival = regs.e();
    values[R_H].ival = regs.h();
    values[R_L].ival = regs.l();

    values[R_IXH].ival = regs.ixh();
    values[R_IXL].ival = regs.ixl();
    values[R_IYH].ival = regs.iyh();
    values[R_IYL].ival = regs.iyl();

    values[R_AF2].ival = regs.af2();
    values[R_BC2].ival = regs.bc2();
    values[R_DE2].ival = regs.de2();
    values[R_HL2].ival = regs.hl2();

    values[R_I].ival = regs.i();
    values[R_R].ival = regs.r();

    values[R_SP].ival = regs.sp();
    values[R_IP].ival = regs.ip();

    values[R_BANK].ival = regs.bank();
#endif

#ifdef DEBUG_68K
    DmaInfo dma;

    if (client && !client->get_dma_info(&dma)) {
      return DRC_FAILED;
    }

    values[R_VDP_DMA_LEN].ival = dma.len();
    values[R_VDP_DMA_SRC].ival = dma.src();
    values[R_VDP_WRITE_ADDR].ival = dma.dst();
#endif
  }

#ifdef DEBUG_68K
  if (clsmask & RC_VDP) {
    VdpRegs regs;

    if (client && !client->get_vdp_regs(&regs)) {
      return DRC_FAILED;
    }

    values[R_V00].ival = regs.v00();
    values[R_V01].ival = regs.v01();
    values[R_V02].ival = regs.v02();
    values[R_V03].ival = regs.v03();
    values[R_V04].ival = regs.v04();
    values[R_V05].ival = regs.v05();
    values[R_V06].ival = regs.v06();
    values[R_V07].ival = regs.v07();
    values[R_V08].ival = regs.v08();
    values[R_V09].ival = regs.v09();
    values[R_V10].ival = regs.v0a();
    values[R_V11].ival = regs.v0b();
    values[R_V12].ival = regs.v0c();
    values[R_V13].ival = regs.v0d();
    values[R_V14].ival = regs.v0e();
    values[R_V15].ival = regs.v0f();
    values[R_V16].ival = regs.v10();
    values[R_V17].ival = regs.v11();
    values[R_V18].ival = regs.v12();
    values[R_V19].ival = regs.v13();
    values[R_V20].ival = regs.v14();
    values[R_V21].ival = regs.v15();
    values[R_V22].ival = regs.v16();
    values[R_V23].ival = regs.v17();
  }
#endif

  return DRC_OK;
}

// Write one thread register
//	tid	- thread id
//	regidx - register index
//	regval - new value of the register
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static drc_t idaapi write_register(thid_t tid, int regidx, const regval_t *value, qstring *errbuf) {
#ifdef DEBUG_68K
  if (regidx >= R_D0 && regidx <= R_SR) {
#endif
    if (client && !client->set_gp_reg((GpRegsEnum)regidx, value->ival & 0xFFFFFFFF)) {
      return DRC_FAILED;
    }
#ifdef DEBUG_68K
  }
  else if (regidx >= R_V00 && regidx <= R_V23) {
    if (client && !client->set_vdp_reg((VdpRegsEnum)(regidx - R_V00), value->ival & 0xFF)) {
      return DRC_FAILED;
    }
  }
#endif

  return DRC_OK;
}

//
// The following functions manipulate bytes in the memory.
//
// Get information on the memory areas
// The debugger module fills 'areas'. The returned vector MUST be sorted.
// Returns:
//   -3: use idb segmentation
//   -2: no changes
//   -1: the process does not exist anymore
//	0: failed
//	1: new memory layout is returned
// This function is called from debthread
static drc_t idaapi get_memory_info(meminfo_vec_t &areas, qstring *errbuf) {
  areas.clear();

  memory_info_t info;

  // Don't remove this loop
  for (int i = 0; i < get_segm_qty(); ++i) {
    segment_t* segm = getnseg(i);

    info.start_ea = segm->start_ea;
    info.end_ea = segm->end_ea;

    qstring buf;
    get_segm_name(&buf, segm);
    info.name = buf;

    get_segm_class(&buf, segm);
    info.sclass = buf;

    info.sbase = 0;
    info.perm = SEGPERM_READ | SEGPERM_WRITE;
    info.bitness = 1;
    areas.push_back(info);
  }
  // Don't remove this loop

#ifdef DEBUG_68K
  info.name = "DBG_VDP_VRAM";
  info.start_ea = BREAKPOINTS_BASE;
  info.end_ea = info.start_ea + 0x10000;
  info.bitness = 1;
  areas.push_back(info);

  info.name = "DBG_VDP_CRAM";
  info.start_ea = info.end_ea;
  info.end_ea = info.start_ea + 0x10000;
  info.bitness = 1;
  areas.push_back(info);

  info.name = "DBG_VDP_VSRAM";
  info.start_ea = info.end_ea;
  info.end_ea = info.start_ea + 0x10000;
  info.bitness = 1;
  areas.push_back(info);
#endif

  if (areas.size() > 1) {
    std::sort(areas.begin(), areas.end(), [](const memory_info_t &a, const memory_info_t &b) {
      return a.start_ea < b.start_ea || (a.start_ea == b.start_ea && a.end_ea < b.end_ea);
    });
  }

  return DRC_OK;
}

// Read process memory
// Returns number of read bytes
// 0 means read error
// -1 means that the process does not exist anymore
// This function is called from debthread
static ssize_t idaapi read_memory(ea_t ea, void *buffer, size_t size, qstring *errbuf) {
  if (errbuf)
    errbuf->clear();
  if (size == 0)
    return 0;
  // The RPC uses 32-bit addresses and lengths; never silently truncate IDA's
  // 64-bit arguments or allow the emulator's address arithmetic to wrap.
  if (ea > UINT32_MAX || size > UINT32_MAX || size > (uint64(1) << 32) - ea) {
    if (errbuf)
      *errbuf = "Memory read range exceeds the 32-bit debugger protocol";
    return 0;
  }
  if (!buffer || !client) {
    if (errbuf)
      *errbuf = !client ? "Emulator is not connected" : "Memory read buffer is null";
    return 0;
  }
  return client->read_memory(uint32_t(ea), uint32_t(size), static_cast<uint8_t*>(buffer), errbuf);
}

// Write process memory
// Returns number of written bytes, -1-fatal error
// This function is called from debthread
static ssize_t idaapi write_memory(ea_t ea, const void *buffer, size_t size, qstring *errbuf) {
  return client && client->write_memory((const uint8_t*)buffer, (uint32_t)ea, (uint32_t)size) ? size : 0;
}

// Is it possible to set breakpoint?
// Returns: BPT_...
// This function is called from debthread or from the main thread if debthread
// is not running yet.
// It is called to verify hardware breakpoints.
static int idaapi is_ok_bpt(bpttype_t type, ea_t ea, int len) {
  switch (type) {
  case BPT_EXEC:
  case BPT_READ: // there is no such constant in sdk61
  case BPT_WRITE:
  case BPT_RDWR:
    return BPT_OK;
  }

  return BPT_BAD_TYPE;
}

// Add/del breakpoints.
// bpts array contains nadd bpts to add, followed by ndel bpts to del.
// returns number of successfully modified bpts, -1-network error
// This function is called from debthread
static drc_t idaapi update_bpts(int* nbpts, update_bpt_info_t *bpts, int nadd, int ndel, qstring *errbuf) {
  for (int i = 0; i < nadd; ++i) {
    if (bpts[i].code == BPT_SKIP) {
      continue;
    }

    ea_t start = bpts[i].ea;
    ea_t end = bpts[i].ea + bpts[i].size - 1;

    BpType type1 = BpType::BP_PC;
    int type2 = 0;
#ifdef DEBUG_68K
    bool is_vdp = false;
#endif

    switch (bpts[i].type) {
    case BPT_EXEC:
      type1 = BpType::BP_PC;
      break;
    case BPT_READ:
      type1 = BpType::BP_READ;
      break;
    case BPT_WRITE:
      type1 = BpType::BP_WRITE;
      break;
    case BPT_RDWR:
      type1 = BpType::BP_READ;
      type2 = (int)BpType::BP_WRITE;
      break;
    }

#ifdef DEBUG_68K
    if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000) {
      start -= BREAKPOINTS_BASE;
      end -= BREAKPOINTS_BASE;
      is_vdp = true;
    }
#endif

    DbgBreakpoint bp;
    bp.set_bstart(start & 0xFFFFFF);
    bp.set_bend(end & 0xFFFFFF);
    bp.set_type(type1);

    bp.set_enabled(true);
#ifdef DEBUG_68K
    bp.set_is_vdp(is_vdp);
#endif

    bpt_t bpt;
    if (get_bpt(start, &bpt) && !bpt.cndbody.empty()) {
        bp.set_elang((uint32_t)bpt.get_cnd_elang_idx());
        bp.set_condition(bpt.cndbody.c_str());
    }

    if (client && !client->add_breakpoint(bp)) {
      return DRC_FAILED;
    }

    if (type2 != 0) {
      bp.set_type((BpType)type2);

      if (client && !client->add_breakpoint(bp)) {
        return DRC_FAILED;
      }
    }

    bpts[i].code = BPT_OK;
  }

  for (int i = 0; i < ndel; ++i) {
    if (bpts[nadd + i].code == BPT_SKIP) {
      continue;
    }

    ea_t start = bpts[nadd + i].ea;
    ea_t end = bpts[nadd + i].ea + bpts[nadd + i].size - 1;
    BpType type1 = BpType::BP_PC;
    int type2 = 0;
#ifdef DEBUG_68K
    bool is_vdp = false;
#endif

    switch (bpts[nadd + i].type) {
    case BPT_EXEC:
      type1 = BpType::BP_PC;
      break;
    case BPT_READ:
      type1 = BpType::BP_READ;
      break;
    case BPT_WRITE:
      type1 = BpType::BP_WRITE;
      break;
    case BPT_RDWR:
      type1 = BpType::BP_READ;
      type2 = (int)BpType::BP_WRITE;
      break;
    }

#ifdef DEBUG_68K
    if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000) {
      start -= BREAKPOINTS_BASE;
      end -= BREAKPOINTS_BASE;
      is_vdp = true;
    }
#endif

    DbgBreakpoint bp;
    bp.set_bstart(start & 0xFFFFFF);
    bp.set_bend(end & 0xFFFFFF);
    bp.set_type(type1);

    bp.set_enabled(true);
#ifdef DEBUG_68K
    bp.set_is_vdp(is_vdp);
#endif

    bpt_t bpt;
    if (get_bpt(start, &bpt) && !bpt.cndbody.empty()) {
        bp.set_elang((uint32_t)bpt.get_cnd_elang_idx());
        bp.set_condition(bpt.cndbody.c_str());
    }

    if (client && !client->del_breakpoint(bp)) {
      return DRC_FAILED;
    }

    if (type2 != 0) {
      bp.set_type((BpType)type2);

      if (client && !client->del_breakpoint(bp)) {
        return DRC_FAILED;
      }
    }

    bpts[nadd + i].code = BPT_OK;
  }

  *nbpts = (ndel + nadd);
  return DRC_OK;
}

// Update low-level (server side) breakpoint conditions
// Returns nlowcnds. -1-network error
// This function is called from debthread
//static drc_t idaapi update_lowcnds(int* nupdated, const lowcnd_t *lowcnds, int nlowcnds, qstring* errbuf) {
//  for (int i = 0; i < nlowcnds; ++i) {
//    ea_t start = lowcnds[i].ea;
//    ea_t end = lowcnds[i].ea + lowcnds[i].size - 1;
//    BpType type1 = BpType::BP_PC;
//    int type2 = 0;
//#ifdef DEBUG_68K
//    bool is_vdp = false;
//#endif
//
//    switch (lowcnds[i].type) {
//    case BPT_EXEC:
//      type1 = BpType::BP_PC;
//      break;
//    case BPT_READ:
//      type1 = BpType::BP_READ;
//      break;
//    case BPT_WRITE:
//      type1 = BpType::BP_WRITE;
//      break;
//    case BPT_RDWR:
//      type1 = BpType::BP_READ;
//      type2 = (int)BpType::BP_WRITE;
//      break;
//    }
//
//#ifdef DEBUG_68K
//    if (start >= BREAKPOINTS_BASE && end < BREAKPOINTS_BASE + 0x30000) {
//      start -= BREAKPOINTS_BASE;
//      end -= BREAKPOINTS_BASE;
//      is_vdp = true;
//    }
//#endif
//
//    DbgBreakpoint bp;
//    bp.set_bstart(start & 0xFFFFFF);
//    bp.set_bend(end & 0xFFFFFF);
//    bp.set_type(type1);
//
//    bp.set_enabled(true);
//#ifdef DEBUG_68K
//    bp.set_is_vdp(is_vdp);
//#endif
//    bp.set_is_forbid(lowcnds[i].cndbody.empty() ? false : ((lowcnds[i].cndbody[0] == '1') ? true : false));
//
//    if (client && !client->update_breakpoint(bp)) {
//      return DRC_FAILED;
//    }
//
//    if (type2 != 0) {
//      bp.set_type((BpType)type2);
//
//      if (client && !client->update_breakpoint(bp)) {
//        return DRC_FAILED;
//      }
//    }
//  }
//
//  *nupdated = nlowcnds;
//  return DRC_OK;
//}

// Calculate the call stack trace
// This function is called when the process is suspended and should fill
// the 'trace' object with the information about the current call stack.
// Returns: true-ok, false-failed.
// If this function is missing or returns false, IDA will use the standard
// mechanism (based on the frame pointer chain) to calculate the stack trace
// This function is called from the main thread
static drc_t idaapi update_call_stack(thid_t tid, call_stack_t *trace) {
  std::vector<uint32_t> cs;

  if (client && !client->get_callstack(cs)) {
    return DRC_FAILED;
  }

  trace->resize(cs.size());

  for (size_t i = 0; i < cs.size(); i++) {
    call_stack_info_t& ci = (*trace)[i];
    ci.callea = cs[i];
    ci.funcea = BADADDR;
    ci.fp = BADADDR;
    ci.funcok = true;
  }

  return DRC_OK;
}

ssize_t idaapi idd_listener_t::on_event(ssize_t code, va_list va) {
  drc_t retcode = DRC_NONE;
  qstring* errbuf;

  switch (code) {
  case debugger_t::ev_init_debugger: {
    const char* hostname = va_arg(va, const char*);

    int portnum = va_arg(va, int);
    const char* password = va_arg(va, const char*);
    errbuf = va_arg(va, qstring*);
    QASSERT(1522, errbuf != NULL);
    retcode = init_debugger(hostname, portnum, password, errbuf);
  }
  break;

  case debugger_t::ev_term_debugger:
    retcode = term_debugger();
    break;

  case debugger_t::ev_get_processes: {
    procinfo_vec_t* procs = va_arg(va, procinfo_vec_t*);
    errbuf = va_arg(va, qstring*);
    retcode = s_get_processes(procs, errbuf);
  }
  break;

  case debugger_t::ev_start_process: {
    const char* path = va_arg(va, const char*);
    const char* args = va_arg(va, const char*);
    const char* startdir = va_arg(va, const char*);
    uint32 dbg_proc_flags = va_arg(va, uint32);
    const char* input_path = va_arg(va, const char*);
    uint32 input_file_crc32 = va_arg(va, uint32);
    errbuf = va_arg(va, qstring*);
    retcode = s_start_process(path,
      args,
      startdir,
      dbg_proc_flags,
      input_path,
      input_file_crc32,
      errbuf);
  }
  break;

  //case debugger_t::ev_attach_process: {
  //  pid_t pid = va_argi(va, pid_t);
  //  int event_id = va_arg(va, int);
  //  uint32 dbg_proc_flags = va_arg(va, uint32);
  //  errbuf = va_arg(va, qstring*);
  //  retcode = s_attach_process(pid, event_id, dbg_proc_flags, errbuf);
  //}
  //break;

  //case debugger_t::ev_detach_process:
  //  retcode = g_dbgmod.dbg_detach_process();
  //  break;

  case debugger_t::ev_get_debapp_attrs: {
    debapp_attrs_t* out_pattrs = va_arg(va, debapp_attrs_t*);
#ifdef DEBUG_68K
    out_pattrs->addrsize = 4;
    out_pattrs->is_be = true;
#else
    out_pattrs->addrsize = 2;
    out_pattrs->is_be = false;
#endif
    out_pattrs->platform = "sega_md";
    out_pattrs->cbsize = sizeof(debapp_attrs_t);
    retcode = DRC_OK;
  }
  break;

  //case debugger_t::ev_rebase_if_required_to: {
  //  ea_t new_base = va_arg(va, ea_t);
  //  retcode = DRC_OK;
  //}
  //break;

  case debugger_t::ev_request_pause:
    errbuf = va_arg(va, qstring*);
    retcode = prepare_to_pause_process(errbuf);
    break;

  case debugger_t::ev_exit_process:
    errbuf = va_arg(va, qstring*);
    retcode = emul_exit_process(errbuf);
    break;

  case debugger_t::ev_get_debug_event: {
    gdecode_t* code = va_arg(va, gdecode_t*);
    debug_event_t* event = va_arg(va, debug_event_t*);
    int timeout_ms = va_arg(va, int);
    *code = get_debug_event(event, timeout_ms);
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_resume: {
    debug_event_t* event = va_arg(va, debug_event_t*);
    retcode = continue_after_event(event);
  }
  break;

  //case debugger_t::ev_set_exception_info: {
  //  exception_info_t* info = va_arg(va, exception_info_t*);
  //  int qty = va_arg(va, int);
  //  g_dbgmod.dbg_set_exception_info(info, qty);
  //  retcode = DRC_OK;
  //}
  //break;

  case debugger_t::ev_suspended: {
    bool dlls_added = va_arg(va, int) != 0;
    stopped_at_debug_event(dlls_added);
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_thread_suspend: {
    thid_t tid = va_argi(va, thid_t);
    pause_execution();
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_thread_continue: {
    thid_t tid = va_argi(va, thid_t);
    continue_execution();
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_set_resume_mode: {
    thid_t tid = va_argi(va, thid_t);
    resume_mode_t resmod = va_argi(va, resume_mode_t);
    retcode = s_set_resume_mode(tid, resmod);
  }
  break;

  case debugger_t::ev_read_registers: {
    thid_t tid = va_argi(va, thid_t);
    int clsmask = va_arg(va, int);
    regval_t* values = va_arg(va, regval_t*);
    errbuf = va_arg(va, qstring*);
    retcode = read_registers(tid, clsmask, values, errbuf);
  }
  break;

  case debugger_t::ev_write_register: {
    thid_t tid = va_argi(va, thid_t);
    int regidx = va_arg(va, int);
    const regval_t* value = va_arg(va, const regval_t*);
    errbuf = va_arg(va, qstring*);
    retcode = write_register(tid, regidx, value, errbuf);
  }
  break;

  case debugger_t::ev_get_memory_info: {
    meminfo_vec_t* ranges = va_arg(va, meminfo_vec_t*);
    errbuf = va_arg(va, qstring*);
    retcode = get_memory_info(*ranges, errbuf);
  }
  break;

  case debugger_t::ev_read_memory: {
    size_t* nbytes = va_arg(va, size_t*);
    ea_t ea = va_arg(va, ea_t);
    void* buffer = va_arg(va, void*);
    size_t size = va_arg(va, size_t);
    errbuf = va_arg(va, qstring*);
    ssize_t code = read_memory(ea, buffer, size, errbuf);
    *nbytes = code >= 0 ? code : 0;
    retcode = code >= 0 ? DRC_OK : DRC_NOPROC;
  }
  break;

  case debugger_t::ev_write_memory: {
    size_t* nbytes = va_arg(va, size_t*);
    ea_t ea = va_arg(va, ea_t);
    const void* buffer = va_arg(va, void*);
    size_t size = va_arg(va, size_t);
    errbuf = va_arg(va, qstring*);
    ssize_t code = write_memory(ea, buffer, size, errbuf);
    *nbytes = code >= 0 ? code : 0;
    retcode = code >= 0 ? DRC_OK : DRC_NOPROC;
  }
  break;

  case debugger_t::ev_check_bpt: {
    int* bptvc = va_arg(va, int*);
    bpttype_t type = va_argi(va, bpttype_t);
    ea_t ea = va_arg(va, ea_t);
    int len = va_arg(va, int);
    *bptvc = is_ok_bpt(type, ea, len);
    retcode = DRC_OK;
  }
  break;

  case debugger_t::ev_update_bpts: {
    int* nbpts = va_arg(va, int*);
    update_bpt_info_t* bpts = va_arg(va, update_bpt_info_t*);
    int nadd = va_arg(va, int);
    int ndel = va_arg(va, int);
    errbuf = va_arg(va, qstring*);
    retcode = update_bpts(nbpts, bpts, nadd, ndel, errbuf);
  }
  break;

  /*case debugger_t::ev_update_lowcnds: {
      int* nupdated = va_arg(va, int*);
      const lowcnd_t* lowcnds = va_arg(va, const lowcnd_t*);
      int nlowcnds = va_arg(va, int);
      errbuf = va_arg(va, qstring*);
      retcode = update_lowcnds(nupdated, lowcnds, nlowcnds, errbuf);
  }
  break;*/

  case debugger_t::ev_update_call_stack: {
    thid_t tid = va_argi(va, thid_t);
    call_stack_t* trace = va_arg(va, call_stack_t*);
    retcode = update_call_stack(tid, trace);
  }
  break;

  //case debugger_t::ev_eval_lowcnd: {
  //  thid_t tid = va_argi(va, thid_t);
  //  ea_t ea = va_arg(va, ea_t);
  //  errbuf = va_arg(va, qstring*);
  //  retcode = g_dbgmod.dbg_eval_lowcnd(tid, ea, errbuf);
  //}
  //break;

  //case debugger_t::ev_bin_search: {
  //  ea_t* ea = va_arg(va, ea_t*);
  //  ea_t start_ea = va_arg(va, ea_t);
  //  ea_t end_ea = va_arg(va, ea_t);
  //  const compiled_binpat_vec_t* ptns = va_arg(va, const compiled_binpat_vec_t*);
  //  int srch_flags = va_arg(va, int);
  //  errbuf = va_arg(va, qstring*);
  //  if (ptns != NULL) {
  //    retcode = g_dbgmod.dbg_bin_search(ea, start_ea, end_ea, *ptns, srch_flags, errbuf);
  //  }
  //}
  //break;
  default:
    retcode = DRC_NONE;
  }

  return retcode;
}

//--------------------------------------------------------------------------
//
//	  DEBUGGER DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------

debugger_t debugger = {
  IDD_INTERFACE_VERSION,
  NAME,
#ifdef DEBUG_68K
  0x8000 + 1,
  "m68k",
#else
  0x8000 + 2,
  "z80",
#endif
  DBG_FLAG_NOHOST | DBG_FLAG_CAN_CONT_BPT | DBG_FLAG_SAFE | DBG_FLAG_FAKE_ATTACH | DBG_FLAG_NOPASSWORD | DBG_FLAG_NOSTARTDIR | DBG_FLAG_NOPARAMETERS | DBG_FLAG_ANYSIZE_HWBPT | DBG_FLAG_DEBTHREAD | DBG_FLAG_PREFER_SWBPTS /*| DBG_FLAG_LOWCNDS*/ |
  DBG_HAS_GET_PROCESSES | DBG_HAS_REQUEST_PAUSE | DBG_HAS_SET_RESUME_MODE | DBG_HAS_CHECK_BPT | DBG_HAS_THREAD_SUSPEND | DBG_HAS_THREAD_CONTINUE,

  register_classes,
  RC_GENERAL,
  registers,
  qnumber(registers),

  0x1000,

  NULL,
  0,
  0,

  DBG_RESMOD_STEP_INTO | DBG_RESMOD_STEP_OVER,
};
