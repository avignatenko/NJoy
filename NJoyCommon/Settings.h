#pragma once


#include <boost/property_tree/ptree.hpp>

class Settings: public boost::property_tree::ptree
{
public:

    static const Settings& instance();

    static void setPath(const std::string &path);
private:
    Settings();


};