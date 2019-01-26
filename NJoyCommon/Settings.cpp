#include "stdafx.h"
#include "Settings.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/dll.hpp>


const Settings& Settings::instance()
{
    static Settings s_settings;
    return s_settings;
}

namespace 
{
    std::string s_path = "settings.json";
}

void Settings::setPath(const std::string& path)
{
    s_path = path;
}

Settings::Settings()
{
    // change path to exe path
    current_path(boost::dll::program_location().parent_path());

    // load settings
    read_json(s_path, static_cast<boost::property_tree::ptree&>(*this));
}
