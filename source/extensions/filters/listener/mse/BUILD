load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP inspector filter for sniffing HTTP protocol and setting HTTP version to a FilterChain.

envoy_extension_package()

envoy_cc_library(
    name = "mse_lib",
    srcs = ["mse.cc"],
    hdrs = ["mse.h"],
    external_deps = ["llhttp"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http:header_utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":mse_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/extensions/filters/listener/mse/v3:pkg_cc_proto",
    ],
)
