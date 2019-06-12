#include "Stream.h"

#include "connection/stream/Processable.h"
#include "connection/stream/Parallel.h"
#include "connection/stream/External.h"
#include "connection/stream/Distributed.h"
#include "connection/stream/ParallelProcess.h"
#include "connection/stream/PureDistributed.h"
#include "connection/Loader.h"

#include "Node.h"

namespace {
    using namespace Gadgetron::Core;
    using namespace Gadgetron::Server::Connection;
    using namespace Gadgetron::Server::Connection::Stream;

    class NodeProcessable : public Processable {
    public:
        NodeProcessable(std::unique_ptr<Node> node, std::string name) : node(std::move(node)), name_(std::move(name)) {}

        void process(
                InputChannel input,
                OutputChannel output,
                ErrorHandler &
        ) override {
             node->process(input, output);
        }

        const std::string& name() override {
            return name_;

        }

    private:
        std::unique_ptr<Node> node;
        const std::string name_;
    };

    std::shared_ptr<Processable> load_node(const Config::Gadget &conf, const Context &context, Loader &loader) {
        auto factory = loader.load_factory<Loader::generic_factory<Node>>("gadget_factory_export_", conf.classname,
                                                                          conf.dll);
        return std::make_shared<NodeProcessable>(factory(context, conf.properties), Config::name(conf));
    }

    std::shared_ptr<Processable> load_node(const Config::Parallel &conf, const Context &context, Loader &loader) {
        return std::make_shared<Gadgetron::Server::Connection::Stream::Parallel>(conf, context, loader);
    }

    std::shared_ptr<Processable> load_node(const Config::External &conf, const Context &context, Loader &loader) {
        return std::make_shared<Gadgetron::Server::Connection::Stream::External>(conf, context, loader);
    }

    std::shared_ptr<Processable> load_node(const Config::Distributed &conf, const Context &context, Loader &loader) {
        return std::make_shared<Gadgetron::Server::Connection::Stream::Distributed>(conf, context, loader);
    }

    std::shared_ptr<Processable> load_node(const Config::ParallelProcess& conf, const Context& context, Loader& loader){
        return std::make_shared<Gadgetron::Server::Connection::Stream::ParallelProcess>(conf,context,loader);
    }

    std::shared_ptr<Processable> load_node(const Config::PureDistributed& conf, const Context& context, Loader& loader){
        return std::make_shared<Gadgetron::Server::Connection::Stream::PureDistributed>(conf,context,loader);
    }
}

namespace Gadgetron::Server::Connection::Stream {

    Stream::Stream(const Config::Stream &config, const Core::Context &context, Loader &loader) : key(config.key) {
        if (config.nodes.empty()) throw std::runtime_error("Empty config provided");
        for (auto &node_config : config.nodes) {
            nodes.emplace_back(
                    boost::apply_visitor([&](auto n) { return load_node(n, context, loader); }, node_config)
            );
        }
    }

    void Stream::process(
            InputChannel input,
            OutputChannel output,
            ErrorHandler &error_handler
    ) {
        std::vector<InputChannel> input_channels;
        input_channels.emplace_back(std::move(input));
        std::vector<OutputChannel> output_channels{};

        for (auto i = 0; i < nodes.size()-1; i++) {
            auto channel = make_channel<MessageChannel>();
            input_channels.emplace_back(std::move(channel.input));
            output_channels.emplace_back(std::move(channel.output));
        }

        output_channels.emplace_back(std::move(output));

        ErrorHandler nested_handler{error_handler, key};

        std::vector<std::thread> threads(nodes.size());
        for (auto i = 0; i < nodes.size(); i++) {
            threads[i] = Processable::process_async(nodes[i],std::move(input_channels[i]),std::move(output_channels[i]),nested_handler);
        }

        for (auto &thread : threads) {
            thread.join();
        }
    }
}

const std::string &Gadgetron::Server::Connection::Stream::Stream::name() {
    return key;
}