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


int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

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

    udp::socket socket(io_context);
    socket.open(udp::v4());
    
    if (SDL_Init(SDL_INIT_JOYSTICK) < 0)
    {
        LOG(ERROR) << "Couldn't initialize SDL: " << SDL_GetError();
        exit(-1);
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        LOG(INFO) << "  " << i << " = " <<  SDL_JoystickNameForIndex(i);
    }

    int joyId = Settings::instance().get<int>("Hardware.JoyId");
    LOG(INFO) << "Capturing device " << joyId;

    SDL_JoystickEventState(SDL_ENABLE);
    SDL_Joystick* joystick = SDL_JoystickOpen(joyId);

    SDL_Event event;
 
    bool end = false;
    int pollingDelayMs = Settings::instance().get<int>("Hardware.PollingDelayMs");
    LOG(INFO) << "Polling delay " << pollingDelayMs;
    std::string serializeBuffer;

    LOG(INFO) << "Starting event processing cycle";

    for(;!end;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollingDelayMs));

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

            default: ;
            }
        }

        if (dataList.data_size() == 0) continue;

        dataList.SerializeToString(&serializeBuffer);
        socket.send_to(boost::asio::buffer(serializeBuffer.data(), serializeBuffer.size()), receiver_endpoint);
    }
   
    /* End loop here */
}
