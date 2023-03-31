#include "source/common/http/http1/llhttp_parser_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace Http1 {
namespace {

// No-op parser callbacks.
class TestParserCallbacks : public ParserCallbacks {
  CallbackResult onMessageBegin() override { return CallbackResult::Success; };
  CallbackResult onUrl(const char*, size_t) override { return CallbackResult::Success; };
  CallbackResult onHeaderField(const char*, size_t) override { return CallbackResult::Success; };
  CallbackResult onHeaderValue(const char*, size_t) override { return CallbackResult::Success; };
  CallbackResult onStatus(const char*, size_t) override {return CallbackResult::Success;};
  CallbackResult onHeadersComplete() override { return CallbackResult::Success; };
  void bufferBody(const char*, size_t) override{};
  CallbackResult onMessageComplete() override { return CallbackResult::Success; };
  void onChunkHeader(bool) override{};
};

class RequestParserImplTest : public testing::Test {
public:
  RequestParserImplTest() {
    parser_ = std::make_unique<LlhttpHttpParserImpl>(MessageType::Request, &callbacks_);
  }
  TestParserCallbacks callbacks_;
  std::unique_ptr<LlhttpHttpParserImpl> parser_;
};

TEST_F(RequestParserImplTest, TestExecute) {
  const char* request = "GET / HTTP/1.1\r\n\r\n";
  int request_len = strlen(request);

  parser_->execute(request, request_len);
  EXPECT_EQ(parser_->getStatus(), ParserStatus::Ok);
  EXPECT_EQ(parser_->methodName(), "GET");
  EXPECT_TRUE(parser_->isHttp11());
}

TEST_F(RequestParserImplTest, TestContentLength) {
  const char* request = "POST / HTTP/1.1\r\nContent-Length: 003\r\n\r\n";
  int request_len = strlen(request);

  parser_->execute(request, request_len);
  EXPECT_EQ(parser_->getStatus(), ParserStatus::Ok);
  EXPECT_TRUE(parser_->contentLength().has_value());
  EXPECT_EQ(parser_->contentLength().value(), 3);
}

TEST_F(RequestParserImplTest, TestErrorName) {
  // Duplicate Content-Length causes error.
  const char* request = "POST / HTTP/1.1\r\nContent-Length: 003\r\nContent-Length: 001\r\n";
  int request_len = strlen(request);
  parser_->execute(request, request_len);
  EXPECT_EQ(parser_->getStatus(), ParserStatus::Error);
  EXPECT_EQ(parser_->errorMessage(), "HPE_UNEXPECTED_CONTENT_LENGTH");
}

TEST_F(RequestParserImplTest, TestChunked) {
  const char* request = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\na\r\n0\r\n\r\n";
  int request_len = strlen(request);
  parser_->execute(request, request_len);
  EXPECT_EQ(parser_->getStatus(), ParserStatus::Ok);
  EXPECT_TRUE(parser_->hasTransferEncoding());
  EXPECT_TRUE(parser_->isChunked());
}

class ResponseParserImplTest : public testing::Test {
public:
  ResponseParserImplTest() {
    parser_ = std::make_unique<LlhttpHttpParserImpl>(MessageType::Response, &callbacks_);
  }
  TestParserCallbacks callbacks_;
  std::unique_ptr<LlhttpHttpParserImpl> parser_;
};

TEST_F(ResponseParserImplTest, TestStatus) {
  const char* response = "HTTP/1.1 200 OK\r\n\r\n";
  int response_len = strlen(response);
  parser_->execute(response, response_len);
  EXPECT_EQ(parser_->getStatus(), ParserStatus::Ok);
  EXPECT_EQ(parser_->statusCode(), 200);
}

}
} // namespace Http1
} // namespace Http
} // namespace Envoy
