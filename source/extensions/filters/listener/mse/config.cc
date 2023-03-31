#include "envoy/extensions/filters/listener/mse/v3/mse.pb.h"
#include "envoy/extensions/filters/listener/mse/v3/mse.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/mse/mse.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace Mse {

/**
 * Config registration for the Mse filter. @see NamedNetworkFilterConfigFactory.
 */
class MseConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    ConfigSharedPtr config(std::make_shared<Config>(context.scope()));
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::mse::v3::Mse>();
  }

  std::string name() const override { return "envoy.filters.listener.mse"; }
};

/**
 * Static registration for the http inspector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MseConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.mse"};

} // namespace Mse
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
