load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "mse_test",
    srcs = ["mse_test.cc"],
    extension_names = ["envoy.filters.listener.mse"],
    #TODO(davinci26): The test passes on Windows *but* http inspector
    # *used* to rely on Event::FileTriggerType::Edge and we got away with it
    # because we mock the dispatcher. Need to verify that the scenario is
    # actually working.
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/extensions/filters/listener/mse:mse_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)
