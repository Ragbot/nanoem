/*
  Copyright (c) 2015-2023 hkrn All rights reserved

  This file is part of emapp component and it's licensed under Mozilla Public License. see LICENSE.md for more details.
 */

#include "../common.h"

#include "emapp/ApplicationPreference.h"
#include "emapp/BaseApplicationService.h"
#include "emapp/FileUtils.h"

using namespace nanoem;
using namespace test;

TEST_CASE("application_configuration_is_persisted", "[emapp][misc]")
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/nanoem_test_configuration/config.json", NANOEM_TEST_OUTPUT_PATH);
    FileUtils::deleteFile(path);
    JSON_Value *root = json_value_init_object();
    JSON_Object *rootObject = json_value_get_object(root);
    json_object_dotset_string(rootObject, "sapp.config.path", path);
    {
        Application application(root);
        ApplicationPreference preference(&application);
        CHECK_FALSE(preference.isNaturalScrollingEnabled());
        preference.setNaturalScrollingEnabled(true);
        preference.setUndoSoftLimit(42);
        REQUIRE(preference.isNaturalScrollingEnabled());
        REQUIRE(application.saveApplicationConfiguration());
    }
    JSON_Value *loaded = json_parse_file(path);
    REQUIRE(loaded != nullptr);
    CHECK(json_value_get_type(loaded) == JSONObject);
    const JSON_Object *loadedObject = json_object(loaded);
    CHECK(json_object_dotget_boolean(loadedObject, "application.preference.mouse.natural-scroll") == 1);
    CHECK(json_object_dotget_number(loadedObject, "application.preference.undo.limit") == 42);
    CHECK(String(json_object_dotget_string(loadedObject, "sapp.config.path")) == String(path));
    json_value_free(loaded);
    json_value_free(root);
    FileUtils::deleteFile(path);
}

TEST_CASE("application_audio_output_device_is_persisted", "[emapp][misc]")
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/nanoem_test_configuration/audio_device.json", NANOEM_TEST_OUTPUT_PATH);
    FileUtils::deleteFile(path);
    JSON_Value *root = json_value_init_object();
    JSON_Object *rootObject = json_value_get_object(root);
    json_object_dotset_string(rootObject, "sapp.config.path", path);
    {
        Application application(root);
        ApplicationPreference preference(&application);
        /* the current system output device is used unless another one is explicitly chosen */
        CHECK(String(preference.audioOutputDevice()) == String("default"));
        {
            const StringList devices(preference.allAvailableAudioOutputDevices());
            REQUIRE(devices.size() == 1);
            CHECK(devices[0] == String("default"));
        }
        /* device list published by the sapp side on startup */
        JSON_Value *devicesValue = json_value_init_array();
        JSON_Array *devicesArray = json_value_get_array(devicesValue);
        json_array_append_string(devicesArray, "default");
        json_array_append_string(devicesArray, "hw:0,0");
        json_array_append_string(devicesArray, "pulse");
        JSON_Object *pending = json_object(application.applicationPendingChangeConfiguration());
        REQUIRE(json_object_dotset_value(pending, "audio.output-devices", devicesValue) == JSONSuccess);
        {
            const StringList devices(preference.allAvailableAudioOutputDevices());
            REQUIRE(devices.size() == 3);
            CHECK(devices[0] == String("default"));
            CHECK(devices[1] == String("hw:0,0"));
            CHECK(devices[2] == String("pulse"));
        }
        preference.setAudioOutputDevice("pulse");
        REQUIRE(String(preference.audioOutputDevice()) == String("pulse"));
        REQUIRE(application.saveApplicationConfiguration());
    }
    JSON_Value *loaded = json_parse_file(path);
    REQUIRE(loaded != nullptr);
    const JSON_Object *loadedObject = json_object(loaded);
    CHECK(String(json_object_dotget_string(loadedObject, "application.preference.audio.output-device")) ==
        String("pulse"));
    const JSON_Array *loadedDevices = json_object_dotget_array(loadedObject, "audio.output-devices");
    REQUIRE(loadedDevices != nullptr);
    REQUIRE(json_array_get_count(loadedDevices) == 3);
    CHECK(String(json_array_get_string(loadedDevices, 2)) == String("pulse"));
    json_value_free(loaded);
    json_value_free(root);
    FileUtils::deleteFile(path);
}

TEST_CASE("application_audio_output_devices_are_described", "[emapp][misc]")
{
    JSON_Value *root = json_value_init_object();
    {
        Application application(root);
        ApplicationPreference preference(&application);
        JSON_Value *devicesValue = json_value_init_array();
        JSON_Array *devicesArray = json_value_get_array(devicesValue);
        /* the sapp side publishes one { name, description } object per real output device */
        JSON_Value *entryValue = json_value_init_object();
        JSON_Object *entry = json_value_get_object(entryValue);
        json_object_set_string(entry, "name", "plughw:CARD=sofhdadsp,DEV=0");
        json_object_set_string(entry, "description", "sof-hda-dsp: HDA Analog");
        json_array_append_value(devicesArray, entryValue);
        /* entries written before descriptions existed are plain names */
        json_array_append_string(devicesArray, "default");
        JSON_Object *pending = json_object(application.applicationPendingChangeConfiguration());
        REQUIRE(json_object_dotset_value(pending, "audio.output-devices", devicesValue) == JSONSuccess);
        const StringList devices(preference.allAvailableAudioOutputDevices());
        REQUIRE(devices.size() == 2);
        CHECK(devices[0] == String("plughw:CARD=sofhdadsp,DEV=0"));
        CHECK(devices[1] == String("default"));
        CHECK(preference.audioOutputDeviceDescription("plughw:CARD=sofhdadsp,DEV=0") ==
            String("sof-hda-dsp: HDA Analog"));
        CHECK(preference.audioOutputDeviceDescription("default") == String("default"));
        CHECK(preference.audioOutputDeviceDescription("not-a-known-device") == String("not-a-known-device"));
        CHECK(preference.audioOutputDeviceDescription(nullptr) == String("default"));
    }
    json_value_free(root);
}

TEST_CASE("application_telemetry_is_disabled", "[emapp][misc]")
{
    JSON_Value *root = json_value_init_object();
    {
        Application application(root);
        ApplicationPreference preference(&application);
        /* no telemetry is ever collected nor reported */
        CHECK_FALSE(preference.isAnalyticsEnabled());
        CHECK_FALSE(preference.isCrashReportEnabled());
    }
    json_value_free(root);
}
