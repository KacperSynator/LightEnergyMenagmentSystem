#include "lamp_controller.h"

namespace {
using DataPacket = light_energy_menagment_system_DataPacket;
using LampData = light_energy_menagment_system_LampData;

bool encode_string(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
    const char* str = (const char*)(*arg);

    if (!pb_encode_tag_for_field(stream, field))
        return false;

    return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

const std::string EncodeDataPacket(const DataPacket& data) {
    uint8_t buffer[256];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, light_energy_menagment_system_DataPacket_fields, &data)) {
        Serial.println("failed to encode data packet");
        return "Encode failed!";
    }

    Serial.printf("Message length: %d\n", stream.bytes_written);
    std::string result {buffer, buffer + stream.bytes_written};

    for(int i = 0; i < stream.bytes_written; i++){
        Serial.printf("%c",buffer[i]);
    }
    Serial.println();
    
    // Serial.print("\nSize: ");
    // Serial.println(result.size());

    // for (const auto& c : result) {
    //     Serial.print(c);
    // }
    // Serial.println();

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
    return strdup(WiFi.macAddress().c_str());
}

void SetupDevice(DataPacket& data_packet) {
    data_packet = light_energy_menagment_system_DataPacket_init_zero;
    data_packet.has_device = true;
    data_packet.device.name.arg = (void*) kDeviceName;
    data_packet.device.name.funcs.encode = &encode_string;
    data_packet.device.mac.arg = (void*) GetMacAddress();
    data_packet.device.mac.funcs.encode = &encode_string;
}

float CalculateDutyCycle(const int& threshold,
                         const float& current_duty_cycle,
                         const float& illuminance)
{
    float relative_max_lux = illuminance / (current_duty_cycle + 0.1);
    float new_duty_cycle = static_cast<float>(threshold) / (relative_max_lux + 0.1);

    if (new_duty_cycle > 1.0) new_duty_cycle = 1.0;
    if (new_duty_cycle < 0.0) new_duty_cycle = 0.0;

    return new_duty_cycle;
}

}  // namespace


void LampController::Setup() {
    setup_status_.all_clear = (
        (setup_status_.wire = Wire.begin()) 
        && (setup_status_.light_meter = light_meter_.begin()) 
        && (setup_status_.lamp_dim = lamp_dim_.Setup()) 
        && (setup_status_.energy_meter = (pzem_.readAddress() != 0x00))
    );

    pinMode(kRelayPin, OUTPUT);
    pinMode(kPirPin, OUTPUT);
    
    ble_connection_.Setup();

    SetupDevice(data_packet_);

    pzem_.resetEnergy();
}

void LampController::Loop() {
    delay(5000);

    if (!setup_status_.all_clear) {
        Serial.println(setup_status_.str().c_str());
        // ble_connection_.SendData(setup_status_.str());
        // Setup();
        // return;
    }

    LampData lamp_data = light_energy_menagment_system_LampData_init_zero;
    lamp_data.illuminance = light_meter_.readLightLevel();

    // Serial.println(pzem_.readAddress(true), HEX);

    if (!ReadEnergyMeterData(lamp_data, pzem_)) {
        Serial.println("Failed to read energy meter data!");
    }

    float duty = CalculateDutyCycle(lux_threshold_, dim_duty_cycle_, lamp_data.illuminance);
    Serial.printf("Current duty: %f\n", dim_duty_cycle_);
    Serial.printf("Calculated duty: %f\n", duty);
    Serial.printf("Illuminance: %f\n", lamp_data.illuminance);
    
    if (duty < 0.1) {
        digitalWrite(kRelayPin, HIGH);
        lamp_dim_.DutyCycle(0.1);
        dim_duty_cycle_ = 0.0;
    } else {
        digitalWrite(kRelayPin, LOW);
        lamp_dim_.DutyCycle(duty);
        dim_duty_cycle_ = duty;
    }

    data_packet_.has_lamp_data = true;
    data_packet_.lamp_data = std::move(lamp_data);

    ble_connection_.SendData(EncodeDataPacket(data_packet_));
}

const std::string LampController::SetupStatus::str() {
    std::stringstream ss;
    ss << std::boolalpha 
       << "Initialisation failed SetupStatus:\n"
       << "\tWire: " << wire << "\n"
       << "\tEnergy meter: " << energy_meter << "\n"
       << "\tLight meter: " << light_meter << "\n"
       << "\tLamp dim:" << lamp_dim << "\n";

    return ss.str();
}
