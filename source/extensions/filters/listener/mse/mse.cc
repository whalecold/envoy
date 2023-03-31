#include "source/extensions/filters/listener/mse/mse.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace Mse {

struct llhttp_settings_s settings_;

class HttpParserSettings {
public:
  HttpParserSettings();
  static int onMessageBegin(llhttp_t*);
  static int onHeaderField(llhttp_t*, const char*, size_t);
  static int onHeaderValue(llhttp_t*, const char*, size_t);
  static int onHeadersComplete(llhttp_t*);
};

HttpParserSettings::HttpParserSettings() {
  settings_.on_message_begin = &HttpParserSettings::onMessageBegin;
  settings_.on_header_field = &HttpParserSettings::onHeaderField;
  settings_.on_header_value = &HttpParserSettings::onHeaderValue;
  settings_.on_headers_complete = &HttpParserSettings::onHeadersComplete;
}

static HttpParserSettings initObj;

int HttpParserSettings::onMessageBegin(llhttp_t* parser) {
  Filter* p = static_cast<Filter*>(parser->data);
  return p->handleMessageBegin();
}

int HttpParserSettings::onHeaderField(llhttp_t* parser, const char* at, size_t length) {
  Filter* p = static_cast<Filter*>(parser->data);
  return p->handleHeaderField(at, length);
}

int HttpParserSettings::onHeaderValue(llhttp_t* parser, const char* at, size_t length) {
  Filter* p = static_cast<Filter*>(parser->data);
  return p->handleHeaderValue(at, length);
}

int HttpParserSettings::onHeadersComplete(llhttp_t* parser) {
  Filter* p = static_cast<Filter*>(parser->data);
  return p->handleHeadersComplete();
}

Config::Config(Stats::Scope& scope)
    : stats_{ALL_HTTP_INSPECTOR_STATS(POOL_COUNTER_PREFIX(scope, "mse."))} {}


Filter::Filter(const ConfigSharedPtr config) : config_(config) {
  llhttp_init(&parser_, HTTP_REQUEST, &settings_);
  parser_.data = this;
}

int Filter::handleMessageBegin() {
  m_curField.clear();
  m_curValue.clear();
  m_lastWasValue = true;
  return 0;
}

int Filter::handleHeaderField(const char* at, size_t length) {
  if (m_lastWasValue) {
    if (!m_curField.empty()) {
      completeLastHeader(m_curField, m_curValue);
    }
    m_curField.clear();
    m_curValue.clear();
  }
  absl::string_view field{at, length};
  m_curField = absl::AsciiStrToLower(field);
  m_lastWasValue = 0;
  return 0;
}

int Filter::handleHeaderValue(const char* at, size_t length) {
  m_curValue.append(at, length);
  m_lastWasValue = 1;
  return 0;
}

int Filter::handleHeadersComplete() {
  if (!m_curField.empty()) {
    completeLastHeader(m_curField, m_curValue);
  }
  return 0;
}

int Filter::completeLastHeader(const std::string& field, const std::string& value) {
  ENVOY_LOG(debug, "completeLastHeader: {} {}", field, value);
  m_headers.insert(std::make_pair(field, value));
  return 0;
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  const char* buf = static_cast<const char*>(raw_slice.mem_);
  const auto parse_state = parseHttpHeader(absl::string_view(buf, raw_slice.len_));
  switch (parse_state) {
  case ParseState::Error:
    // Invalid HTTP preface found, then just continue for next filter.
    done(false);
    return Network::FilterStatus::Continue;
  case ParseState::Done:
    done(true);
    return Network::FilterStatus::Continue;
  case ParseState::Continue:
    return Network::FilterStatus::StopIteration;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "mse: new connection accepted");

  const Network::ConnectionSocket& socket = cb.socket();

  const absl::string_view transport_protocol = socket.detectedTransportProtocol();
  if (!transport_protocol.empty() && transport_protocol != "raw_buffer") {
    ENVOY_LOG(trace, "mse: cannot inspect http protocol with transport socket {}",
              transport_protocol);
    return Network::FilterStatus::Continue;
  }

  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

ParseState Filter::parseHttpHeader(absl::string_view data) {
    if (data[0] == '\r' || data[0] == '\n') {
      return ParseState::Error;
    }

    absl::string_view new_data = data;
    const size_t pos = data.find("\r\n\r\n");
    if (pos != absl::string_view::npos) {
      // Include \r\n\r\n
      new_data = new_data.substr(0, pos + 4);

      llhttp_errno_t error;
      size_t nread = new_data.length();
      error = llhttp_execute(&parser_, new_data.data(), new_data.length());

      // Errors in parsing HTTP.
      if (error != HPE_OK && error != HPE_PAUSED && error != HPE_STRICT && error != HPE_INTERNAL) {
        nread = llhttp_get_error_pos(&parser_) - new_data.data();
        ENVOY_LOG(trace, "mse: http_parser parsed {} chars, error code: {}", nread, error);
        return ParseState::Error;
      }
      ENVOY_LOG(trace, "mse: http_parser parsed {} chars, error code: {}", nread, error);
      // deal with mse redirect
      resolveMseRedirect();
      return ParseState::Done;
    } else {
      // invalid http
      ENVOY_LOG(debug, "mse: invalid http request line.");
      return ParseState::Error;
    }
  
}

void Filter::resolveMseRedirect() {
  // Mse custom : Get x-mse-original-dst-host & restore local address
  const std::string request_override_host =
      findHeaderValue(Http::Headers::get().MseOriginalDstHost.get());
  restoreMseLocalAddress(request_override_host);
  // clear headers
  if (!m_headers.empty()) {
    m_headers.clear();
  }
}

std::string Filter::findHeaderValue(const std::string& key) {
  auto iter = m_headers.find(key);
  if (iter != m_headers.end()) {
    return iter->second;
  }
  return "";
}

void Filter::restoreMseLocalAddress(const std::string& host) {
  if (host.empty()) {
    ENVOY_LOG(debug, "mse: override header value is empty {}");
    return;
  }
  Network::Address::InstanceConstSharedPtr request_host =
      Network::Utility::parseInternetAddressAndPortNoThrow(host, false);
  if (request_host != nullptr) {
    ENVOY_LOG(debug, "mse: using mse header to rediret to other listener: {}.", host);
    cb_->socket().connectionInfoProvider().restoreLocalAddress(request_host);
  } else {
    ENVOY_LOG(debug, "mse: invalid override header value. {}", host);
  }
}

void Filter::done(bool success) { ENVOY_LOG(trace, "mse: done: {}", success); }

} // namespace Mse
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
