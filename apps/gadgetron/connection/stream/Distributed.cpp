//
// Created by dchansen on 1/16/19.
//

#include "Distributed.h"

namespace {
    std::vector<Gadgetron::Server::Distributed::Address> get_workers() {
        return {{"localhost", "9002"},
                {"localhost", "9002"}};
    }


    class ChannelCreatorFunction : public Gadgetron::Core::Distributed::ChannelCreator {
    public:
        template<class F>
        explicit ChannelCreatorFunction(F func) : func(func) {}

        std::shared_ptr<Gadgetron::Core::OutputChannel> create() override {
            return func();
        }

        std::function<std::shared_ptr<Gadgetron::Core::OutputChannel>(void)> func;

    };


    void
    process_channel(std::shared_ptr<Gadgetron::Core::Channel> input, std::shared_ptr<Gadgetron::Core::Channel> output) {

        Gadgetron::Core::InputChannel &in_view = *input;
        for (auto &&message : in_view)
            output->push_message(std::move(message));
    }

}

void Gadgetron::Server::Connection::Stream::Distributed::process(std::shared_ptr<Gadgetron::Core::Channel> input,
                                                                 std::shared_ptr<Gadgetron::Core::Channel> output,
                                                                 Gadgetron::Server::Connection::ErrorHandler &error_handler) {
    std::vector<std::thread> threads;
    std::vector<std::shared_ptr<Core::Channel>> channels;

    std::vector<std::shared_ptr<Stream>> streams;
//
    auto creator = ChannelCreatorFunction([&]() {
        auto channel = this->create_remote_channel();

        threads.emplace_back(error_handler.run("ChannelReader", process_channel, channel, output));
        channels.push_back(channel);
        return channel;
    });

//    auto creator = ChannelCreatorFunction([&]() {
//
//        auto stream = std::make_shared<Stream>(stream_config,context,loader);
//        auto channel = std::make_shared<Core::MessageChannel>();
//        auto out_channel = std::make_shared<Core::MessageChannel>();
//
//        threads.emplace_back(error_handler.run("Stream", [&,channel,stream](){
//            stream->process(channel,output,error_handler);
//            }));
//
//        channels.push_back(channel);
//        return channel;
//    });
    error_handler.handle("Distributor", [&]() { distributor->process(*input, creator, *output); });

    input->close();
    for (auto channel : channels)
        channel->close();

    for (auto &t : threads)
        t.join();

    output->close();

}

Gadgetron::Server::Connection::Stream::Distributed::Distributed(const Config::Distributed &distributed_config,
                                                                const Gadgetron::Core::Context &context,
                                                                Gadgetron::Server::Connection::Loader &loader)
        : loader(loader), context(context),
          xml_config{serialize_config(
                  Config{distributed_config.readers, distributed_config.writers, distributed_config.stream})},
          workers{get_workers()}, current_worker{make_cyclic(workers.begin(), workers.end())} {
    distributor = load_distributor(distributed_config.distributor);

    readers = loader.load_readers(distributed_config);
    writers = loader.load_writers(distributed_config);


}

std::unique_ptr<Gadgetron::Core::Distributed::Distributor>
Gadgetron::Server::Connection::Stream::Distributed::load_distributor(
        const Gadgetron::Server::Connection::Config::Distributor &conf) {
    auto factory = loader.load_factory<Loader::generic_factory<Core::Distributed::Distributor>>(
            "distributor_factory_export_", conf.classname, conf.dll);
    return factory(context, conf.properties);
}

std::shared_ptr<Gadgetron::Server::Distributed::RemoteChannel>
Gadgetron::Server::Connection::Stream::Distributed::create_remote_channel() {

    auto previous_worker = current_worker;
    while (true) {
        try {
            auto result = std::make_shared<RemoteChannel>(*current_worker, xml_config, context.header, readers,
                                                          writers);
            ++current_worker;
            return result;
        } catch (const std::runtime_error &) {
            if (current_worker == previous_worker) throw;
            ++current_worker;
        }
    }
}