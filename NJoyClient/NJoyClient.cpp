// NJoyClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <glog/logging.h>

#include <boost/asio.hpp>
using boost::asio::ip::udp;

#include <google/protobuf/stubs/common.h>
#include <NJoyCommon/protocol.pb.h>
#include <NJoyCommon/Settings.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>

#undef main

class Client
{
public:
    Client(boost::asio::io_context& io_context, udp::endpoint& receiver_endpoint)
        : m_context(io_context)
        , m_socket(io_context)
        , m_remote_endpoint(receiver_endpoint)
        , t(io_context)
    {
        m_socket.open(udp::v4());
        setupPingServer();
    }

    void send(const NJoy::JoyListData& dataList)
    {
        dataList.SerializeToString(&m_serializeBuffer);

        m_socket.async_send_to(
            boost::asio::buffer(m_serializeBuffer.data(), m_serializeBuffer.size()),
            m_remote_endpoint, [](
                const boost::system::error_code& error,
                std::size_t bytes_transferred)
        {
            if (error)
            {
                LOG(ERROR) << "Send error: " << error.message();
            }
        });
    }

private:

    std::function<void(const boost::system::error_code &)> waitHandler;
    std::function<void(const boost::system::error_code&, std::size_t)> onPingReceived;
    boost::asio::steady_timer t;

    void setupPingServer()
    {
        int pingTime = Settings::instance().get<int>("Client.PingDelayMs");
        LOG(INFO) << "Ping time " << pingTime;

        waitHandler = [this, pingTime](const boost::system::error_code &)
        {
            // send ping
            NJoy::JoyListData dataList;
            NJoy::JoyData* data = dataList.add_data();
            data->set_type(NJoy::PING);
            data->mutable_ping()->set_token(1);

            LOG(INFO) << "Sending ping";
            send(dataList);

            t.expires_at(t.expiry() + boost::asio::chrono::milliseconds(pingTime));
            t.async_wait(waitHandler);
        };

        t.expires_after(boost::asio::chrono::milliseconds(pingTime));
        t.async_wait(waitHandler);

        onPingReceived = [this](const boost::system::error_code&, std::size_t len)
        {
            NJoy::JoyListData list;
            list.ParseFromArray(m_recv_buf.data(), len);
            for (int i = 0; i < list.data_size(); ++i)
            {
                const NJoy::JoyData& data = list.data(i);
                if (data.type() == NJoy::PING)
                {
                    LOG(INFO) << "Ping received with token " << data.ping().token();
                }
            }

            m_socket.async_receive(boost::asio::buffer(m_recv_buf), onPingReceived);
        };

        m_socket.async_receive(boost::asio::buffer(m_recv_buf), onPingReceived);
    }


private:
    boost::asio::io_context& m_context;
    udp::socket m_socket;
    udp::endpoint& m_remote_endpoint;
    std::array<char, 256> m_recv_buf = {};
    std::string m_serializeBuffer;

};


int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    try
    {
        // Initialize Google's logging library.
        google::InitGoogleLogging(argv[0]);
        FLAGS_logtostderr = true;

        Settings::setPath("settings_client.json");

        boost::asio::io_context io_context;

        std::string host = Settings::instance().get<std::string>("Server.Host");
        std::string port = Settings::instance().get<std::string>("Server.Port");

        udp::resolver resolver(io_context);
        udp::endpoint receiver_endpoint =
            *resolver.resolve(udp::v4(), host, port).begin();

        Client client(io_context, receiver_endpoint);

        if (SDL_Init(SDL_INIT_JOYSTICK) < 0)
        {
            LOG(ERROR) << "Couldn't initialize SDL: " << SDL_GetError();
            exit(-1);
        }

        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            LOG(INFO) << "  " << i << " = " << SDL_JoystickNameForIndex(i);
        }

        int joyId = Settings::instance().get<int>("Hardware.JoyId");
        LOG(INFO) << "Capturing device " << joyId;

        SDL_JoystickEventState(SDL_ENABLE);
        SDL_Joystick* joystick = SDL_JoystickOpen(joyId);

        LOG(INFO) << "Starting event processing cycle";

        bool end = false;
        int pollingDelayMs = Settings::instance().get<int>("Hardware.PollingDelayMs");
        LOG(INFO) << "Polling delay " << pollingDelayMs;
        SDL_Event event;

        for (; !end;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollingDelayMs));

            io_context.poll();

            NJoy::JoyListData dataList;

            while (SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_JOYHATMOTION:
                {
                    NJoy::JoyData* data = dataList.add_data();
                    data->set_type(NJoy::HAT);
                    data->mutable_hat()->set_index(event.jhat.hat);
                    data->mutable_hat()->set_value(event.jhat.value);
                    break;
                }
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                {
                    NJoy::JoyData* data = dataList.add_data();
                    data->set_type(NJoy::BUTTON);
                    data->mutable_button()->set_index(event.jbutton.button);
                    data->mutable_button()->set_value(event.jbutton.state == SDL_PRESSED ? true : false);
                    break;
                }
                case SDL_QUIT:
                    end = true;
                    break;

                default:;
                }
            }

            if (dataList.data_size() == 0) continue;

            client.send(dataList);


        }

    }
    catch (std::exception& e)
    {
        LOG(ERROR) << e.what();
        return -1;
    }
}
