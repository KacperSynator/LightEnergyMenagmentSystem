mod db_handler;
mod mqtt_connection;
mod proto_utils;
pub mod server_rpi;

include!(concat!(env!("OUT_DIR"), "/protos/mod.rs"));
use light_energy_management_system::{
    DataPacket, Device, DeviceMeasurments, DeviceType, Measurement, MeasurementStatus,
    MeasurementType,
};
