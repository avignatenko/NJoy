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

int initJoystick(int devId)
{
    // Get the driver attributes (Vendor ID, Product ID, Version Number)
    if (!vJoyEnabled())
    {
        LOG(ERROR) << "Function vJoyEnabled Failed - make sure that vJoy is installed and enabled";
        return -1;
    }
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
        printf("vJoy device %d is already owned by another feeder\nCannot continue\n", devId);
        return -3;
    case VJD_STAT_MISS:
        printf("vJoy device %d is not installed or disabled\nCannot continue\n", devId);
        return -4;
    default:
        printf("vJoy device %d general error\nCannot continue\n", devId);
        return -1;
    };

    // Acquire the vJoy device
    if (!AcquireVJD(devId))
    {
        printf("Failed to acquire vJoy device number %d.\n", devId);
        return -1;
    }
    else
        printf("Acquired device number %d - OK\n", devId);

    return 0;
}


int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Initialize Google's logging library.
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    Settings::setPath("settings_server.json");

    LOG(INFO) << "Starting...";



    int devId = Settings::instance().get<int>("vJoy.DeviceID");

    int res = initJoystick(devId);
    if (res < 0) return res;

    // start server

    try
    {
        boost::asio::io_context io_context;

        int port = Settings::instance().get<int>("Server.Port");

        udp::socket socket(io_context, udp::endpoint(udp::v4(), port));

        for (;;)
        {
            std::array<char, 256> recv_buf; // fixme: what is max size?
            udp::endpoint remote_endpoint;
            boost::system::error_code error;
            size_t len = socket.receive_from(boost::asio::buffer(recv_buf),
                                             remote_endpoint, 0, error);

            if (error && error != boost::asio::error::message_size)
                throw boost::system::system_error(error);

            NJoy::JoyListData list;
            list.ParseFromArray(recv_buf.data(), len);

            for (int i = 0; i < list.data_size(); ++i)
            {
                const NJoy::JoyData& data = list.data(i);
                switch (data.type())
                {
                case NJoy::AXIS:
                {
                    auto& axis = data.axis();
                    bool res = SetAxis(axis.value(), devId, axis.value());
                    break;
                }

                case NJoy::BUTTON:
                {
                    auto& button = data.button();
                    bool res = SetBtn(button.value(), devId, button.index());
                    break;
                }

                case NJoy::HAT:
                {
                    auto& hat = data.hat();
                    bool res = SetDiscPov(hat.value(), devId, hat.index());
                    break;
                }

                }

            }

        }
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
}
