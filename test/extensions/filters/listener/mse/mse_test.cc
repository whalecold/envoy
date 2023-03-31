#include "source/common/common/hex.h"
#include "source/common/http/utility.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/extensions/filters/listener/mse/mse.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace Mse {
namespace {

class MseTest : public testing::Test {
public:
  MseTest()
      : cfg_(std::make_shared<Config>(store_)),
        io_handle_(
            Network::SocketInterfaceImpl::makePlatformSpecificSocket(42, false, absl::nullopt)) {}
  ~MseTest() override { io_handle_->close(); }

  void init() {
    filter_ = std::make_unique<Filter>(cfg_);

    EXPECT_CALL(cb_, socket()).WillRepeatedly(ReturnRef(socket_));
    EXPECT_CALL(socket_, detectedTransportProtocol()).WillRepeatedly(Return("raw_buffer"));
    EXPECT_CALL(cb_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_CALL(testing::Const(socket_), ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(socket_, ioHandle()).WillRepeatedly(ReturnRef(*io_handle_));
    EXPECT_CALL(dispatcher_,
                createFileEvent_(_, _, Event::PlatformDefaultTriggerType,
                                 Event::FileReadyType::Read | Event::FileReadyType::Closed))
        .WillOnce(
            DoAll(SaveArg<1>(&file_event_callback_), ReturnNew<NiceMock<Event::MockFileEvent>>()));
    buffer_ = std::make_unique<Network::ListenerFilterBufferImpl>(
        *io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {},
        filter_->maxReadBytes());
  }

  NiceMock<Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
  Stats::IsolatedStoreImpl store_;
  ConfigSharedPtr cfg_;
  std::unique_ptr<Filter> filter_;
  Network::MockListenerFilterCallbacks cb_;
  Network::MockConnectionSocket socket_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::FileReadyCb file_event_callback_;
  Network::IoHandlePtr io_handle_;
  std::unique_ptr<Network::ListenerFilterBufferImpl> buffer_;
};

TEST_F(MseTest, TestHttp11OverrideHost) {
  init();
  const absl::string_view header =
      "GET /anything HTTP/1.1\r\nuser-agent: curl/7.64.0\r\naccept: "
      "*/*\r\nx-mse-original-dst-host: 127.1.2.3:9080\r\nHost: test.com:9081\r\nx-request-id: "
      "a52df4a0-ed00-4a19-86a7-80e5049c6c84\r\nx-envoy-expected-rq-timeout-ms: "
      "15000\r\ncontent-length: 0\r\n\r\n";


    std::vector<uint8_t> data = Hex::decode(std::string(header));
#ifdef WIN32
    EXPECT_CALL(os_sys_calls_, readv(_, _, _))
        .WillOnce(
            Invoke([&header](os_fd_t fd, const iovec* iov, int iovcnt) -> Api::SysCallSizeResult {
            ASSERT(iov->iov_len >= header.size());
            memcpy(iov->iov_base, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
            }))
        .WillOnce(Return(Api::SysCallSizeResult{ssize_t(-1), SOCKET_ERROR_AGAIN}));
#else
    EXPECT_CALL(os_sys_calls_, recv(42, _, _, MSG_PEEK))
        .WillOnce(Invoke(
            [&header](os_fd_t, void* buffer, size_t length, int) -> Api::SysCallSizeResult {
            ASSERT(length >= header.size());
            memcpy(buffer, header.data(), header.size());
            return Api::SysCallSizeResult{ssize_t(header.size()), 0};
            }));
#endif

  filter_->onAccept(cb_);
  file_event_callback_(Event::FileReadyType::Read);
  filter_->onData(*buffer_);
  EXPECT_EQ(true, socket_.connectionInfoProvider().localAddressRestored());
  EXPECT_EQ("127.1.2.3:9080", socket_.connectionInfoProvider().localAddress()->asString());
}

} // namespace
} // namespace Mse
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
