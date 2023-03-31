#include "source/common/http/http1/llhttp_parser_impl.h"

#include <llhttp.h>

#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

namespace {
ParserStatus intToStatus(int rc) {
  // See
  // https://github.com/nodejs/llhttp/blob/a620012f3fd1b64ace16d31c52cd57b97ee7174c/src/native/api.h#L29-L36
  switch (rc) {
  case -1:
  case HPE_USER:
    return ParserStatus::Error;
  case HPE_OK:
    return ParserStatus::Ok;
  case 1:
    return ParserStatus::Error;
  case 2:
    return ParserStatus::Error;
  case HPE_PAUSED:
    return ParserStatus::Paused;
  default:
    return ParserStatus::Error;
  }
}

int callbackResultToInt(CallbackResult result) {
  switch (result) {
  case CallbackResult::Error:
    return HPE_USER;
  case CallbackResult::Success:
    return HPE_OK;
  case CallbackResult::NoBody:
    return 1;
  case CallbackResult::NoBodyData:
    return 2;
  case CallbackResult::Paused:
    return HPE_PAUSED;
  default:
    return HPE_USER;
  }
}
} // namespace

class LlhttpHttpParserImpl::Impl {
public:
  Impl(llhttp_type_t type) {
    llhttp_init(&parser_, type, &settings_);
    // TODO(langgengxin) set or not?
    llhttp_set_lenient_chunked_length(&parser_, 1);
  }

  Impl(llhttp_type_t type, void* data) : Impl(type) {
    parser_.data = data;
    llhttp_settings_init(&settings_);
    settings_.on_message_begin = [](llhttp_t* parser) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onMessageBegin());
    };
    settings_.on_url = [](llhttp_t* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onUrl(at, length));
    };
    settings_.on_status = [](llhttp_t* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onStatus(at, length));
    };
    settings_.on_header_field = [](llhttp_t* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onHeaderField(at, length));
    };
    settings_.on_header_value = [](llhttp_t* parser, const char* at, size_t length) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onHeaderValue(at, length));
    };
    settings_.on_headers_complete = [](llhttp_t* parser) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onHeadersComplete());
    };

    settings_.on_body = [](llhttp_t* parser, const char* at, size_t length) -> int {
      static_cast<ParserCallbacks*>(parser->data)->bufferBody(at, length);
      return 0;
    };
    settings_.on_message_complete = [](llhttp_t* parser) -> int {
      auto* conn_impl = static_cast<ParserCallbacks*>(parser->data);
      return callbackResultToInt(conn_impl->onMessageComplete());
    };
    settings_.on_chunk_header = [](llhttp_t* parser) -> int {
      // A 0-byte chunk header is used to signal the end of the chunked body.
      // When this function is called, http-parser holds the size of the chunk in
      // parser->content_length. See
      // https://github.com/nodejs/http-parser/blob/v2.9.3/http_parser.h#L336
      const bool is_final_chunk = (parser->content_length == 0);
      static_cast<ParserCallbacks*>(parser->data)->onChunkHeader(is_final_chunk);
      return 0;
    };
  }

  size_t execute(const char* slice, int len) {
    llhttp_errno_t error;
    if (slice == nullptr || len == 0) {
      error = llhttp_finish(&parser_);
    } else {
      error = llhttp_execute(&parser_, slice, len);
    }
    size_t nread = len;
    // Adjust number of bytes read in case of error.
    if (error != HPE_OK) {
      nread = llhttp_get_error_pos(&parser_) - slice;
      // TODO(langgengxin) deal with upgrade?
    }
    return nread;
  }

  void resume() { llhttp_resume(&parser_); }

  CallbackResult pause() {
    // llhttp can only pause by returning a paused status in user callbacks.
    return CallbackResult::Paused;
  }

  int getErrno() { return llhttp_get_errno(&parser_); }

  uint16_t statusCode() const { return parser_.status_code; }

  bool isHttp11() const { return parser_.http_major == 1 && parser_.http_minor == 1; }

  int httpMajor() const { return parser_.http_major; }

  int httpMinor() const { return parser_.http_minor; }

  absl::optional<uint64_t> contentLength() const {
    if (!has_content_length_) {
      return absl::nullopt;
    }
    return parser_.content_length;
  }

  void setHasContentLength(bool val) { has_content_length_ = val; }

  bool isChunked() const { return parser_.flags & F_CHUNKED; }

  absl::string_view methodName() const {
    return llhttp_method_name(static_cast<llhttp_method>(parser_.method));
  }

  int hasTransferEncoding() const { return parser_.flags & F_TRANSFER_ENCODING; }

private:
  llhttp_t parser_;
  llhttp_settings_s settings_;
  bool has_content_length_{true};
};

LlhttpHttpParserImpl::LlhttpHttpParserImpl(MessageType type, ParserCallbacks* data) {
  llhttp_type_t parser_type;
  switch (type) {
  case MessageType::Request:
    parser_type = HTTP_REQUEST;
    break;
  case MessageType::Response:
    parser_type = HTTP_RESPONSE;
    break;
  }

  impl_ = std::make_unique<Impl>(parser_type, data);
}

// Because we have a pointer-to-impl using std::unique_ptr, we must place the destructor in the
// same compilation unit so that the destructor has a complete definition of Impl.
LlhttpHttpParserImpl::~LlhttpHttpParserImpl() = default;

size_t LlhttpHttpParserImpl::execute(const char* slice, int len) {
  return impl_->execute(slice, len);
}

void LlhttpHttpParserImpl::resume() { impl_->resume(); }

CallbackResult LlhttpHttpParserImpl::pause() { return impl_->pause(); }

ParserStatus LlhttpHttpParserImpl::getStatus() const { return intToStatus(impl_->getErrno()); }

uint16_t LlhttpHttpParserImpl::statusCode() const { return impl_->statusCode(); }

bool LlhttpHttpParserImpl::isHttp11() const { return impl_->isHttp11(); }

absl::optional<uint64_t> LlhttpHttpParserImpl::contentLength() const {
  return impl_->contentLength();
}

void LlhttpHttpParserImpl::setHasContentLength(bool val) { return impl_->setHasContentLength(val); }

bool LlhttpHttpParserImpl::isChunked() const { return impl_->isChunked(); }

absl::string_view LlhttpHttpParserImpl::methodName() const { return impl_->methodName(); }

absl::string_view LlhttpHttpParserImpl::errorMessage() const {
  return llhttp_errno_name(static_cast<llhttp_errno>(impl_->getErrno()));
}

int LlhttpHttpParserImpl::hasTransferEncoding() const { return impl_->hasTransferEncoding(); }

} // namespace Http1
} // namespace Http
} // namespace Envoy
