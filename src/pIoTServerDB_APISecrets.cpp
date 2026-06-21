//
//  pIoTServerDB_APISecrets.cpp
//  pIoTServer
//
//  Split from pIoTServerDB.cpp
//

#include "pIoTServerDB.hpp"
#include "PropValKeys.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <strings.h>
#include <tuple>
#include <vector>

#include <time.h>

#include "json.hpp"
#include "Utils.hpp"
#include "LogMgr.hpp"
#include "SolarTimeMgr.hpp"
#include "TimeStamp.hpp"
#include "lunar.hpp"
#include "Actuator_Device.hpp"
#include "pIoTServerEvaluator.hpp"

using namespace timestamp;
using namespace nlohmann;
using namespace std;

#define DBL_MAX std::numeric_limits<double>::max()
#define TIME_MAX    std::numeric_limits<time_t>::max()

// MARK: -  API Secrets
// MARK: - API Secrets

bool pIoTServerDB::apiSecretCreate(string APIkey, string APISecret)
{
    if (APIkey.empty() || APISecret.empty()) {
        return false;
    }

    json config;

    if (!getJSONProperty(string(PROP_CONFIG), &config)) {
        return false;
    }

    if (!config.contains(PROP_ACCESS_KEYS) || !config[PROP_ACCESS_KEYS].is_array()) {
        config[PROP_ACCESS_KEYS] = json::array();
    }

    json accessKeys = config[PROP_ACCESS_KEYS];

    for (const auto& ak : accessKeys) {
        if (!ak.is_object()) {
            continue;
        }

        if (!ak.contains(PROP_API_KEY) || !ak[PROP_API_KEY].is_string()) {
            continue;
        }

        if (ak[PROP_API_KEY].get<string>() == APIkey) {
            return false;
        }
    }

    accessKeys.push_back({
        { PROP_API_KEY, APIkey },
        { PROP_API_SECRET, APISecret }
    });

    config[PROP_ACCESS_KEYS] = accessKeys;
    _props.at(PROP_CONFIG) = config;

    _didChangeProperties = true;
    saveProperties();

    return true;
}

bool pIoTServerDB::apiSecretSetSecret(string APIkey, string APISecret)
{
    if (APIkey.empty() || APISecret.empty()) {
        return false;
    }

    json config;

    if (!getJSONProperty(string(PROP_CONFIG), &config)
        || !config.contains(PROP_ACCESS_KEYS)
        || !config[PROP_ACCESS_KEYS].is_array()) {
        return false;
    }

    json accessKeys = config[PROP_ACCESS_KEYS];

    for (auto& ak : accessKeys) {
        if (!ak.is_object()) {
            continue;
        }

        if (!ak.contains(PROP_API_KEY) || !ak[PROP_API_KEY].is_string()) {
            continue;
        }

        if (ak[PROP_API_KEY].get<string>() == APIkey) {
            ak[PROP_API_SECRET] = APISecret;

            config[PROP_ACCESS_KEYS] = accessKeys;
            _props.at(PROP_CONFIG) = config;

            _didChangeProperties = true;
            saveProperties();

            return true;
        }
    }

    return false;
}

bool pIoTServerDB::apiSecretDelete(string APIkey)
{
    if (APIkey.empty()) {
        return false;
    }

    json config;

    if (!getJSONProperty(string(PROP_CONFIG), &config)
        || !config.contains(PROP_ACCESS_KEYS)
        || !config[PROP_ACCESS_KEYS].is_array()) {
        return false;
    }

    json accessKeys = json::array();
    bool found = false;

    for (const auto& ak : config[PROP_ACCESS_KEYS]) {
        if (!ak.is_object()
            || !ak.contains(PROP_API_KEY)
            || !ak[PROP_API_KEY].is_string()) {
            accessKeys.push_back(ak);
            continue;
        }

        if (ak[PROP_API_KEY].get<string>() == APIkey) {
            found = true;
            continue;
        }

        accessKeys.push_back(ak);
    }

    if (!found) {
        return false;
    }

    config[PROP_ACCESS_KEYS] = accessKeys;
    _props.at(PROP_CONFIG) = config;

    _didChangeProperties = true;
    saveProperties();

    return true;
}

bool pIoTServerDB::apiSecretGetSecret(string APIkey, string &APISecret)
{
    json config;

    if (getJSONProperty(string(PROP_CONFIG), &config)
        && config.contains(PROP_ACCESS_KEYS)
        && config[PROP_ACCESS_KEYS].is_array()) {

        const json accessKeys = config[PROP_ACCESS_KEYS];

        for (const auto& ak : accessKeys) {
            if (!ak.is_object()) {
                continue;
            }

            if (!ak.contains(PROP_API_KEY) || !ak.contains(PROP_API_SECRET)) {
                continue;
            }

            if (!ak[PROP_API_KEY].is_string() || !ak[PROP_API_SECRET].is_string()) {
                continue;
            }

            if (ak[PROP_API_KEY].get<string>() == APIkey) {
                APISecret = ak[PROP_API_SECRET].get<string>();
                return true;
            }
        }
    }

    return false;
}


bool pIoTServerDB::apiSecretMustAuthenticate(){

   json config;

   if(getJSONProperty(string(PROP_CONFIG), &config)
      && config.contains(PROP_ACCESS_KEYS)
      &&  config[PROP_ACCESS_KEYS].is_array()
      && config[PROP_ACCESS_KEYS].size() > 0 )
       return true;

   return false;
}
