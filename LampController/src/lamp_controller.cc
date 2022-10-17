#include "lamp_controller.h"

namespace {
using DataPacket = light_energy_menagment_system_DataPacket;
using LampData = light_energy_menagment_system_LampData;

bool encode_string(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const char* str = (const char*)(*arg);

    Serial.printf("not encoded: %s\n", str);

    if (!pb_encode_tag_for_field(stream, field))
        return false;

    return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

const std::string EncodeDataPacket(const DataPacket& data) {
    uint8_t buffer[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, light_energy_menagment_system_DataPacket_fields, &data)){
        Serial.println("failed to encode data packet");
        return "Encode failed!";
    }

    Serial.printf("Message length: %d\n", stream.bytes_written);
    std::string result {buffer, buffer + stream.bytes_written};

    for(int i = 0; i < stream.bytes_written; i++){
        Serial.printf("%c",buffer[i]);
    }
    
    Serial.print("\nSize: ");
    Serial.println(result.size());

    for (const auto& c : result) {
        Serial.print(c);
    }
    Serial.println();

    return result;

}

bool TryReadDataTo(const float& data, float& dest) {
    if (isnan(data)) return false;

    dest = data;
    return true;
}

bool ReadEnergyMeterData(LampData& lamp_data, PZEM004Tv30& energy_meter) {
    bool succes = true;

    succes &= TryReadDataTo(energy_meter.voltage(), lamp_data.voltage);
    succes &= TryReadDataTo(energy_meter.current(),lamp_data.current);
    succes &= TryReadDataTo(energy_meter.power(), lamp_data.power);
    succes &= TryReadDataTo(energy_meter.energy(), lamp_data.energy);
    succes &= TryReadDataTo(energy_meter.frequency(), lamp_data.frequency);
    succes &= TryReadDataTo(energy_meter.pf(), lamp_data.power_factor);

    return succes;
}

bool SetSleepDuration(uint64_t time_in_us) {
    return ESP_OK == esp_sleep_enable_timer_wakeup(time_in_us);
}

const char* GetMacAddress() {
    return WiFi.macAddress().c_str();
}

void SetupDevice(DataPacket& data_packet) {
    char mac[32];
    strcpy(mac, GetMacAddress());

    data_packet = light_energy_menagment_system_DataPacket_init_zero;
    data_packet.has_device = true;
    data_packet.device.name.arg = (void*) "LampController";
    data_packet.device.name.funcs.encode = &encode_string;
    data_packet.device.mac.arg = (void*) mac;
    data_packet.device.mac.funcs.encode = &encode_string;
}

}  // namespace



bool LampController::Setup() {
    if (!Wire.begin()) {
        Serial.println("Wire begin failed!");
        return false;
    }
    
    if (!light_meter_.begin()) {
        Serial.println("Light meter begin failed!");
        return false;
    }

    ble_connection_.Setup();

    SetupDevice(data_packet_);

    pzem_.resetEnergy();

    if (!lamp_dim_.Setup()) {
        Serial.println("Lamp dim pwm setup failed!");
        return false;
    }

    // if (!SetSleepDuration(sleep_duration_ * kMicroSecToSecFactor)) {
    //     Serial.println("Failed to set sleep duration");
    //     return false;
    // }

    return true;
}



void LampController::Loop() {
    LampData lamp_data = light_energy_menagment_system_LampData_init_zero;
    lamp_data.illuminance = light_meter_.readLightLevel();

    if (!ReadEnergyMeterData(lamp_data, pzem_))  {
        Serial.println("Failed to read energy meter data!");
    }

    for(dim_duty_cycle_ = 0; dim_duty_cycle_  <= 1.0; dim_duty_cycle_  += 0.01) {
        lamp_dim_.DutyCycle(dim_duty_cycle_ );
        delay(100);
    }

    data_packet_.has_lamp_data = true;
    data_packet_.lamp_data = std::move(lamp_data);

    ble_connection_.SendData(EncodeDataPacket(data_packet_));
    // esp_deep_sleep_start();
    delay(1000);
}
