/*

NOFUSS Client 0.2.5
Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "NoFUSSClient.h"
#include <functional>
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>

void NoFUSSClientClass::setServer(String server) {
    _server = server;
}

void NoFUSSClientClass::setDevice(String device) {
    _device = device;
}

void NoFUSSClientClass::setVersion(String version) {
    _version = version;
}

void NoFUSSClientClass::onMessage(TMessageFunction fn) {
    _callback = fn;
}

String NoFUSSClientClass::getNewVersion() {
    return _newVersion;
}

String NoFUSSClientClass::getNewFirmware() {
    return _newFirmware;
}

String NoFUSSClientClass::getNewFileSystem() {
    return _newFileSystem;
}

int NoFUSSClientClass::getErrorNumber() {
    return _errorNumber;
}

String NoFUSSClientClass::getErrorString() {
    return _errorString;
}

void NoFUSSClientClass::_doCallback(nofuss_t message) {
    if (_callback != NULL) _callback(message);
}

String NoFUSSClientClass::_getPayload() {

    String payload = "";

    HTTPClient http;
    http.begin((char *) _server.c_str());
    http.useHTTP10(true);
    http.setReuse(false);
    http.setTimeout(HTTP_TIMEOUT);
    http.setUserAgent(F(HTTP_USERAGENT));
    http.addHeader(F("X-ESP8266-MAC"), WiFi.macAddress());
    http.addHeader(F("X-ESP8266-DEVICE"), _device);
    http.addHeader(F("X-ESP8266-VERSION"), _version);
    http.addHeader(F("X-ESP8266-CHIPID"), String(ESP.getChipId()));
    http.addHeader(F("X-ESP8266-CHIPSIZE"), String(ESP.getFlashChipRealSize()));

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        payload = http.getString();
    } else {
        _errorNumber = httpCode;
        _errorString = http.errorToString(httpCode);
    }
    http.end();

    return payload;

}

bool NoFUSSClientClass::_checkUpdates() {

    String payload = _getPayload();
    if (payload.length() == 0) {
        _doCallback(NOFUSS_NO_RESPONSE_ERROR);
        return false;
    }
    bool failed = false;
#if ARDUINOJSON_VERSION_MAJOR == 6
    StaticJsonDocument<500> response;
    auto error = deserializeJson(response, payload);
    if(error){
        failed = true;
    }
   
#else
    StaticJsonBuffer<500> jsonBuffer;
    JsonObject& response = jsonBuffer.parseObject(payload);
    if (!response.success()) {
        failed = true;
    }
#endif

    if (failed) {
        _doCallback(NOFUSS_PARSE_ERROR);
        return false;
    }


    if (response.size() == 0) {
        _doCallback(NOFUSS_UPTODATE);
        return false;
    }
#if ARDUINOJSON_VERSION_MAJOR == 6
    _newVersion = String(response["version"].as<char *>());
    _newFileSystem = String(response["spiffs"].as<char *>());
    _newFirmware = String(response["firmware"].as<char *>());
#else
    _newVersion = response.get<String>("version");
    _newFileSystem = response.get<String>("spiffs");
    _newFirmware = response.get<String>("firmware");
#endif

    _doCallback(NOFUSS_UPDATING);
    return true;

}

void NoFUSSClientClass::_doUpdate() {

    char url[200];
    bool error = false;
    uint8_t updates = 0;

    ESPhttpUpdate.rebootOnUpdate(false);

    if (_newFileSystem.length() > 0) {

        // Update SPIFFS
        sprintf(url, "%s/%s", _server.c_str(), _newFileSystem.c_str());
        t_httpUpdate_return ret = ESPhttpUpdate.updateSpiffs(url);

        if (ret == HTTP_UPDATE_FAILED) {
            error = true;
            _errorNumber = ESPhttpUpdate.getLastError();
            _errorString = ESPhttpUpdate.getLastErrorString();
            _doCallback(NOFUSS_FILESYSTEM_UPDATE_ERROR);
        } else if (ret == HTTP_UPDATE_OK) {
            updates++;
            _doCallback(NOFUSS_FILESYSTEM_UPDATED);
        }

    }

    if (!error && (_newFirmware.length() > 0)) {

        // Update binary
        sprintf(url, "%s%s", _server.c_str(), _newFirmware.c_str());
        t_httpUpdate_return ret = ESPhttpUpdate.update(url);

        if (ret == HTTP_UPDATE_FAILED) {
            error = true;
            _errorNumber = ESPhttpUpdate.getLastError();
            _errorString = ESPhttpUpdate.getLastErrorString();
            _doCallback(NOFUSS_FIRMWARE_UPDATE_ERROR);
        } else if (ret == HTTP_UPDATE_OK) {
            updates++;
            _doCallback(NOFUSS_FIRMWARE_UPDATED);
        }

    }

    if (!error && (updates > 0)) {
        _doCallback(NOFUSS_RESET);
        ESP.restart();
    }

}

void NoFUSSClientClass::handle() {
    _doCallback(NOFUSS_START);
    if (_checkUpdates()) _doUpdate();
    _doCallback(NOFUSS_END);
}

NoFUSSClientClass NoFUSSClient;
