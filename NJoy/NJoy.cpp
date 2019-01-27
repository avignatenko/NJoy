// NJoy.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"

#include <VJoy/inc/public.h>
#include <VJoy/inc/vjoyinterface.h>

#include <cstdio>
#include <iostream>


#define GLOG_NO_ABBREVIATED_SEVERITIES
#include <glog/logging.h>

#include <boost/asio.hpp>
#include <google/protobuf/stubs/common.h>
#include <NJoyCommon/protocol.pb.h>
#include <NJoyCommon/Settings.h>

using boost::asio::ip::udp;

void initJoystick(int devId)
{
    // Get the driver attributes (Vendor ID, Product ID, Version Number)
    if (!vJoyEnabled())
        throw std::runtime_error("Function vJoyEnabled Failed - make sure that vJoy is installed and enabled");
    else
    {
        wprintf(L"Vendor: %s\nProduct :%s\nVersion Number:%s\n", static_cast<TCHAR *> (GetvJoyManufacturerString()), static_cast<TCHAR *>(GetvJoyProductString()), static_cast<TCHAR *>(GetvJoySerialNumberString()));

    };

    // Get the status of the vJoy device before trying to acquire it
    VjdStat status = GetVJDStatus(devId);

    switch (status)
    {
    case VJD_STAT_OWN:
        LOG(INFO) << "vJoy device" << devId << "is already owned by this feeder";
        break;
    case VJD_STAT_FREE:
        printf("vJoy device %d is free\n", devId);
        break;
    case VJD_STAT_BUSY:
        throw std::runtime_error("vJoy device is already owned by another feeder");
    case VJD_STAT_MISS:
        throw std::runtime_error("vJoy device is not installed or disabled");
    default:
        throw std::runtime_error("vJoy device general error");
    };

    // Acquire the vJoy device
    if (!AcquireVJD(devId))
    {
        throw std::runtime_error("Failed to acquire vJoy device number");
    }
    else
    {
        LOG(INFO) << "Acquired device number " << devId;
    }
}

class Server
{
public:

    Server(boost::asio::io_context& io_context, int port, int devId)
        : m_socket(io_context, udp::endpoint(udp::v4(), port))
        , m_devId(devId)
    {
        startReceive();
    }

private:

    static int SDLHatToVJoy(int sdl)
    {
        switch (sdl)
        {
        case 0x00 : return -1;  //   SDL_HAT_CENTERED    0x00
        case 0x01 : return  0; //#define SDL_HAT_UP          0x01
        case 0x02 : return  1; //#define SDL_HAT_RIGHT       0x02
        case 0x04 : return  2; //#define SDL_HAT_DOWN        0x04
        case 0x08 : return  3; //#define SDL_HAT_LEFT        0x08
        default:  return -1;
        }
    }

    void onReceived(const boost::system::error_code& error, std::size_t len)
    {
        // we're not stopping on error, just log it
        if (error)
        {
            LOG(ERROR) << "Receive error: " << error.message();
            startReceive();
            return;
        }

        NJoy::JoyListData list;
        list.ParseFromArray(m_recv_buf.data(), len);

        for (int i = 0; i < list.data_size(); ++i)
        {
            const NJoy::JoyData& data = list.data(i);
            switch (data.type())
            {
            case NJoy::AXIS:
            {
                auto& axis = data.axis();
                bool res = SetAxis(axis.value(), m_devId, axis.index() + 1);
                if (!res)
                {
                    LOG(ERROR) << "Failed to set axis";
                }
                break;
            }

            case NJoy::BUTTON:
            {
                auto& button = data.button();
                bool res = SetBtn(button.value(), m_devId, button.index() + 1);
                if (!res)
                {
                    LOG(ERROR) << "Failed to set button";
                }

                break;
            }

            case NJoy::HAT:
            {
                auto& hat = data.hat();
                bool res = SetDiscPov(SDLHatToVJoy(hat.value()), m_devId, hat.index() + 1);
                if (!res)
                {
                    LOG(ERROR) << "Failed to set hat";
                }

                break;
            }

            case NJoy::PING:
            {
                auto& ping = data.ping();
                LOG(INFO) << "Ping received with token " << ping.token();
                LOG(INFO) << "Sending ping back";
                // send data back to show we're alive
                m_socket.async_send_to(boost::asio::buffer(m_recv_buf, len), m_remote_endpoint, 0,
                                       [](const boost::system::error_code& error, std::size_t len)
                {
                    if (error)
                    {
                        LOG(ERROR) << "Send error: " << error.message();
                    }
                });
            }
            }

        }

        startReceive();
    }

    void startReceive()
    {
        m_socket.async_receive_from(
            boost::asio::buffer(m_recv_buf), m_remote_endpoint,
            [this](const boost::system::error_code& error, std::size_t len)
        {
            onReceived(error, len);
        }
        );
    }
private:

    udp::socket m_socket;
    udp::endpoint m_remote_endpoint;

    int m_devId;
    std::array<char, 256> m_recv_buf = {}; // fixme: what is max size?

};


int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    try
    {

        // Initialize Google's logging library.
        google::InitGoogleLogging(argv[0]);
        FLAGS_logtostderr = true;

        Settings::setPath("settings_server.json");

        LOG(INFO) << "Starting...";

        int devId = Settings::instance().get<int>("vJoy.DeviceID");

        initJoystick(devId);

        boost::asio::io_context io_context;
        const int port = Settings::instance().get<int>("Server.Port");

        Server server(io_context, port, devId);

        io_context.run();

    }
    catch (std::exception& e)
    {
        LOG(ERROR) << e.what();
        return -1;
    }
}
