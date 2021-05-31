/**
 * Autogenerated by Thrift Compiler (0.14.0)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "DbgClient.h"




DbgClient_start_event_args::~DbgClient_start_event_args() noexcept {
}


uint32_t DbgClient_start_event_args::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    xfer += iprot->skip(ftype);
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t DbgClient_start_event_args::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_start_event_args");

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}


DbgClient_start_event_pargs::~DbgClient_start_event_pargs() noexcept {
}


uint32_t DbgClient_start_event_pargs::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_start_event_pargs");

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}


DbgClient_pause_event_args::~DbgClient_pause_event_args() noexcept {
}


uint32_t DbgClient_pause_event_args::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_I32) {
          xfer += iprot->readI32(this->address);
          this->__isset.address = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_MAP) {
          {
            this->changed.clear();
            uint32_t _size39;
            ::apache::thrift::protocol::TType _ktype40;
            ::apache::thrift::protocol::TType _vtype41;
            xfer += iprot->readMapBegin(_ktype40, _vtype41, _size39);
            uint32_t _i43;
            for (_i43 = 0; _i43 < _size39; ++_i43)
            {
              int32_t _key44;
              xfer += iprot->readI32(_key44);
              int32_t& _val45 = this->changed[_key44];
              xfer += iprot->readI32(_val45);
            }
            xfer += iprot->readMapEnd();
          }
          this->__isset.changed = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t DbgClient_pause_event_args::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_pause_event_args");

  xfer += oprot->writeFieldBegin("address", ::apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32(this->address);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("changed", ::apache::thrift::protocol::T_MAP, 2);
  {
    xfer += oprot->writeMapBegin(::apache::thrift::protocol::T_I32, ::apache::thrift::protocol::T_I32, static_cast<uint32_t>(this->changed.size()));
    std::map<int32_t, int32_t> ::const_iterator _iter46;
    for (_iter46 = this->changed.begin(); _iter46 != this->changed.end(); ++_iter46)
    {
      xfer += oprot->writeI32(_iter46->first);
      xfer += oprot->writeI32(_iter46->second);
    }
    xfer += oprot->writeMapEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}


DbgClient_pause_event_pargs::~DbgClient_pause_event_pargs() noexcept {
}


uint32_t DbgClient_pause_event_pargs::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_pause_event_pargs");

  xfer += oprot->writeFieldBegin("address", ::apache::thrift::protocol::T_I32, 1);
  xfer += oprot->writeI32((*(this->address)));
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("changed", ::apache::thrift::protocol::T_MAP, 2);
  {
    xfer += oprot->writeMapBegin(::apache::thrift::protocol::T_I32, ::apache::thrift::protocol::T_I32, static_cast<uint32_t>((*(this->changed)).size()));
    std::map<int32_t, int32_t> ::const_iterator _iter47;
    for (_iter47 = (*(this->changed)).begin(); _iter47 != (*(this->changed)).end(); ++_iter47)
    {
      xfer += oprot->writeI32(_iter47->first);
      xfer += oprot->writeI32(_iter47->second);
    }
    xfer += oprot->writeMapEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}


DbgClient_stop_event_args::~DbgClient_stop_event_args() noexcept {
}


uint32_t DbgClient_stop_event_args::read(::apache::thrift::protocol::TProtocol* iprot) {

  ::apache::thrift::protocol::TInputRecursionTracker tracker(*iprot);
  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_MAP) {
          {
            this->changed.clear();
            uint32_t _size48;
            ::apache::thrift::protocol::TType _ktype49;
            ::apache::thrift::protocol::TType _vtype50;
            xfer += iprot->readMapBegin(_ktype49, _vtype50, _size48);
            uint32_t _i52;
            for (_i52 = 0; _i52 < _size48; ++_i52)
            {
              int32_t _key53;
              xfer += iprot->readI32(_key53);
              int32_t& _val54 = this->changed[_key53];
              xfer += iprot->readI32(_val54);
            }
            xfer += iprot->readMapEnd();
          }
          this->__isset.changed = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t DbgClient_stop_event_args::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_stop_event_args");

  xfer += oprot->writeFieldBegin("changed", ::apache::thrift::protocol::T_MAP, 1);
  {
    xfer += oprot->writeMapBegin(::apache::thrift::protocol::T_I32, ::apache::thrift::protocol::T_I32, static_cast<uint32_t>(this->changed.size()));
    std::map<int32_t, int32_t> ::const_iterator _iter55;
    for (_iter55 = this->changed.begin(); _iter55 != this->changed.end(); ++_iter55)
    {
      xfer += oprot->writeI32(_iter55->first);
      xfer += oprot->writeI32(_iter55->second);
    }
    xfer += oprot->writeMapEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}


DbgClient_stop_event_pargs::~DbgClient_stop_event_pargs() noexcept {
}


uint32_t DbgClient_stop_event_pargs::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  ::apache::thrift::protocol::TOutputRecursionTracker tracker(*oprot);
  xfer += oprot->writeStructBegin("DbgClient_stop_event_pargs");

  xfer += oprot->writeFieldBegin("changed", ::apache::thrift::protocol::T_MAP, 1);
  {
    xfer += oprot->writeMapBegin(::apache::thrift::protocol::T_I32, ::apache::thrift::protocol::T_I32, static_cast<uint32_t>((*(this->changed)).size()));
    std::map<int32_t, int32_t> ::const_iterator _iter56;
    for (_iter56 = (*(this->changed)).begin(); _iter56 != (*(this->changed)).end(); ++_iter56)
    {
      xfer += oprot->writeI32(_iter56->first);
      xfer += oprot->writeI32(_iter56->second);
    }
    xfer += oprot->writeMapEnd();
  }
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void DbgClientClient::start_event()
{
  send_start_event();
}

void DbgClientClient::send_start_event()
{
  int32_t cseqid = 0;
  oprot_->writeMessageBegin("start_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_start_event_pargs args;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();
}

void DbgClientClient::pause_event(const int32_t address, const std::map<int32_t, int32_t> & changed)
{
  send_pause_event(address, changed);
}

void DbgClientClient::send_pause_event(const int32_t address, const std::map<int32_t, int32_t> & changed)
{
  int32_t cseqid = 0;
  oprot_->writeMessageBegin("pause_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_pause_event_pargs args;
  args.address = &address;
  args.changed = &changed;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();
}

void DbgClientClient::stop_event(const std::map<int32_t, int32_t> & changed)
{
  send_stop_event(changed);
}

void DbgClientClient::send_stop_event(const std::map<int32_t, int32_t> & changed)
{
  int32_t cseqid = 0;
  oprot_->writeMessageBegin("stop_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_stop_event_pargs args;
  args.changed = &changed;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();
}

bool DbgClientProcessor::dispatchCall(::apache::thrift::protocol::TProtocol* iprot, ::apache::thrift::protocol::TProtocol* oprot, const std::string& fname, int32_t seqid, void* callContext) {
  ProcessMap::iterator pfn;
  pfn = processMap_.find(fname);
  if (pfn == processMap_.end()) {
    iprot->skip(::apache::thrift::protocol::T_STRUCT);
    iprot->readMessageEnd();
    iprot->getTransport()->readEnd();
    ::apache::thrift::TApplicationException x(::apache::thrift::TApplicationException::UNKNOWN_METHOD, "Invalid method name: '"+fname+"'");
    oprot->writeMessageBegin(fname, ::apache::thrift::protocol::T_EXCEPTION, seqid);
    x.write(oprot);
    oprot->writeMessageEnd();
    oprot->getTransport()->writeEnd();
    oprot->getTransport()->flush();
    return true;
  }
  (this->*(pfn->second))(seqid, iprot, oprot, callContext);
  return true;
}

void DbgClientProcessor::process_start_event(int32_t, ::apache::thrift::protocol::TProtocol* iprot, ::apache::thrift::protocol::TProtocol*, void* callContext)
{
  void* ctx = nullptr;
  if (this->eventHandler_.get() != nullptr) {
    ctx = this->eventHandler_->getContext("DbgClient.start_event", callContext);
  }
  ::apache::thrift::TProcessorContextFreer freer(this->eventHandler_.get(), ctx, "DbgClient.start_event");

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->preRead(ctx, "DbgClient.start_event");
  }

  DbgClient_start_event_args args;
  args.read(iprot);
  iprot->readMessageEnd();
  uint32_t bytes = iprot->getTransport()->readEnd();

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->postRead(ctx, "DbgClient.start_event", bytes);
  }

  try {
    iface_->start_event();
  } catch (const std::exception&) {
    if (this->eventHandler_.get() != nullptr) {
      this->eventHandler_->handlerError(ctx, "DbgClient.start_event");
    }
    return;
  }

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->asyncComplete(ctx, "DbgClient.start_event");
  }

  return;
}

void DbgClientProcessor::process_pause_event(int32_t, ::apache::thrift::protocol::TProtocol* iprot, ::apache::thrift::protocol::TProtocol*, void* callContext)
{
  void* ctx = nullptr;
  if (this->eventHandler_.get() != nullptr) {
    ctx = this->eventHandler_->getContext("DbgClient.pause_event", callContext);
  }
  ::apache::thrift::TProcessorContextFreer freer(this->eventHandler_.get(), ctx, "DbgClient.pause_event");

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->preRead(ctx, "DbgClient.pause_event");
  }

  DbgClient_pause_event_args args;
  args.read(iprot);
  iprot->readMessageEnd();
  uint32_t bytes = iprot->getTransport()->readEnd();

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->postRead(ctx, "DbgClient.pause_event", bytes);
  }

  try {
    iface_->pause_event(args.address, args.changed);
  } catch (const std::exception&) {
    if (this->eventHandler_.get() != nullptr) {
      this->eventHandler_->handlerError(ctx, "DbgClient.pause_event");
    }
    return;
  }

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->asyncComplete(ctx, "DbgClient.pause_event");
  }

  return;
}

void DbgClientProcessor::process_stop_event(int32_t, ::apache::thrift::protocol::TProtocol* iprot, ::apache::thrift::protocol::TProtocol*, void* callContext)
{
  void* ctx = nullptr;
  if (this->eventHandler_.get() != nullptr) {
    ctx = this->eventHandler_->getContext("DbgClient.stop_event", callContext);
  }
  ::apache::thrift::TProcessorContextFreer freer(this->eventHandler_.get(), ctx, "DbgClient.stop_event");

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->preRead(ctx, "DbgClient.stop_event");
  }

  DbgClient_stop_event_args args;
  args.read(iprot);
  iprot->readMessageEnd();
  uint32_t bytes = iprot->getTransport()->readEnd();

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->postRead(ctx, "DbgClient.stop_event", bytes);
  }

  try {
    iface_->stop_event(args.changed);
  } catch (const std::exception&) {
    if (this->eventHandler_.get() != nullptr) {
      this->eventHandler_->handlerError(ctx, "DbgClient.stop_event");
    }
    return;
  }

  if (this->eventHandler_.get() != nullptr) {
    this->eventHandler_->asyncComplete(ctx, "DbgClient.stop_event");
  }

  return;
}

::std::shared_ptr< ::apache::thrift::TProcessor > DbgClientProcessorFactory::getProcessor(const ::apache::thrift::TConnectionInfo& connInfo) {
  ::apache::thrift::ReleaseHandler< DbgClientIfFactory > cleanup(handlerFactory_);
  ::std::shared_ptr< DbgClientIf > handler(handlerFactory_->getHandler(connInfo), cleanup);
  ::std::shared_ptr< ::apache::thrift::TProcessor > processor(new DbgClientProcessor(handler));
  return processor;
}

void DbgClientConcurrentClient::start_event()
{
  send_start_event();
}

void DbgClientConcurrentClient::send_start_event()
{
  int32_t cseqid = 0;
  ::apache::thrift::async::TConcurrentSendSentry sentry(this->sync_.get());
  oprot_->writeMessageBegin("start_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_start_event_pargs args;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();

  sentry.commit();
}

void DbgClientConcurrentClient::pause_event(const int32_t address, const std::map<int32_t, int32_t> & changed)
{
  send_pause_event(address, changed);
}

void DbgClientConcurrentClient::send_pause_event(const int32_t address, const std::map<int32_t, int32_t> & changed)
{
  int32_t cseqid = 0;
  ::apache::thrift::async::TConcurrentSendSentry sentry(this->sync_.get());
  oprot_->writeMessageBegin("pause_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_pause_event_pargs args;
  args.address = &address;
  args.changed = &changed;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();

  sentry.commit();
}

void DbgClientConcurrentClient::stop_event(const std::map<int32_t, int32_t> & changed)
{
  send_stop_event(changed);
}

void DbgClientConcurrentClient::send_stop_event(const std::map<int32_t, int32_t> & changed)
{
  int32_t cseqid = 0;
  ::apache::thrift::async::TConcurrentSendSentry sentry(this->sync_.get());
  oprot_->writeMessageBegin("stop_event", ::apache::thrift::protocol::T_ONEWAY, cseqid);

  DbgClient_stop_event_pargs args;
  args.changed = &changed;
  args.write(oprot_);

  oprot_->writeMessageEnd();
  oprot_->getTransport()->writeEnd();
  oprot_->getTransport()->flush();

  sentry.commit();
}



