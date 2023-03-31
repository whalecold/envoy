#pragma once

#include <memory>

#include "source/common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class LlhttpHttpParserImpl : public Parser {
public:
  LlhttpHttpParserImpl(MessageType type, ParserCallbacks* data);
  ~LlhttpHttpParserImpl() override;

  // Http1::Parser
  size_t execute(const char* data, int len) override;
  void resume() override;
  CallbackResult pause() override;
  ParserStatus getStatus() const override;
  uint16_t statusCode() const override;
  bool isHttp11() const override;
  absl::optional<uint64_t> contentLength() const override;
  void setHasContentLength(bool val) override;
  bool isChunked() const override;
  absl::string_view methodName() const override;
  absl::string_view errorMessage() const override;
  int hasTransferEncoding() const override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
