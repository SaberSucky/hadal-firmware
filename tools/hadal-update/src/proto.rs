//! The Flipper RPC messages this tool uses, from assets/protobuf/{flipper,system,storage,application}.proto.
//! Written out by hand, with only the fields used, so the build needs no protoc.

use prost::{Message, Oneof};

#[derive(Clone, PartialEq, Message)]
pub struct Main {
    #[prost(uint32, tag = "1")]
    pub command_id: u32,
    #[prost(int32, tag = "2")]
    pub command_status: i32,
    #[prost(bool, tag = "3")]
    pub has_next: bool,
    #[prost(
        oneof = "Content",
        tags = "4, 19, 5, 6, 31, 32, 33, 41, 46, 17, 18, 24, 25, 7, 8, 9, 10, 11, 12, 13, 14, 15"
    )]
    pub content: Option<Content>,
}

#[derive(Clone, PartialEq, Oneof)]
pub enum Content {
    #[prost(message, tag = "4")]
    Empty(Empty),
    #[prost(message, tag = "19")]
    StopSession(Empty),
    #[prost(message, tag = "5")]
    PingRequest(Ping),
    #[prost(message, tag = "6")]
    PingResponse(Ping),
    #[prost(message, tag = "31")]
    RebootRequest(RebootRequest),
    #[prost(message, tag = "32")]
    DeviceInfoRequest(Empty),
    #[prost(message, tag = "33")]
    DeviceInfoResponse(DeviceInfoResponse),
    #[prost(message, tag = "41")]
    UpdateRequest(UpdateRequest),
    #[prost(message, tag = "46")]
    UpdateResponse(UpdateResponse),
    #[prost(message, tag = "17")]
    LockStatusRequest(Empty),
    #[prost(message, tag = "18")]
    LockStatusResponse(LockStatusResponse),
    #[prost(message, tag = "24")]
    StatRequest(PathRequest),
    #[prost(message, tag = "25")]
    StatResponse(StatResponse),
    #[prost(message, tag = "7")]
    ListRequest(ListRequest),
    #[prost(message, tag = "8")]
    ListResponse(ListResponse),
    #[prost(message, tag = "9")]
    ReadRequest(ReadRequest),
    #[prost(message, tag = "10")]
    ReadResponse(ReadResponse),
    #[prost(message, tag = "11")]
    WriteRequest(WriteRequest),
    #[prost(message, tag = "12")]
    DeleteRequest(DeleteRequest),
    #[prost(message, tag = "13")]
    MkdirRequest(PathRequest),
    #[prost(message, tag = "14")]
    Md5sumRequest(PathRequest),
    #[prost(message, tag = "15")]
    Md5sumResponse(Md5sumResponse),
}

#[derive(Clone, PartialEq, Message)]
pub struct Empty {}

#[derive(Clone, PartialEq, Message)]
pub struct Ping {
    #[prost(bytes = "vec", tag = "1")]
    pub data: Vec<u8>,
}

pub const REBOOT_MODE_UPDATE: i32 = 2;

#[derive(Clone, PartialEq, Message)]
pub struct RebootRequest {
    #[prost(int32, tag = "1")]
    pub mode: i32,
}

#[derive(Clone, PartialEq, Message)]
pub struct DeviceInfoResponse {
    #[prost(string, tag = "1")]
    pub key: String,
    #[prost(string, tag = "2")]
    pub value: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct UpdateRequest {
    #[prost(string, tag = "1")]
    pub update_manifest: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct UpdateResponse {
    #[prost(int32, tag = "1")]
    pub code: i32,
}

#[derive(Clone, PartialEq, Message)]
pub struct LockStatusResponse {
    #[prost(bool, tag = "1")]
    pub locked: bool,
}

#[derive(Clone, PartialEq, Message)]
pub struct PathRequest {
    #[prost(string, tag = "1")]
    pub path: String,
}

pub const FILE_TYPE_DIR: i32 = 1;

#[derive(Clone, PartialEq, Message)]
pub struct File {
    #[prost(int32, tag = "1")]
    pub file_type: i32,
    #[prost(string, tag = "2")]
    pub name: String,
    #[prost(uint32, tag = "3")]
    pub size: u32,
    #[prost(bytes = "vec", tag = "4")]
    pub data: Vec<u8>,
    #[prost(string, tag = "5")]
    pub md5sum: String,
}

#[derive(Clone, PartialEq, Message)]
pub struct StatResponse {
    #[prost(message, optional, tag = "1")]
    pub file: Option<File>,
}

#[derive(Clone, PartialEq, Message)]
pub struct ListRequest {
    #[prost(string, tag = "1")]
    pub path: String,
    #[prost(bool, tag = "2")]
    pub include_md5: bool,
    #[prost(uint32, tag = "3")]
    pub filter_max_size: u32,
}

#[derive(Clone, PartialEq, Message)]
pub struct ListResponse {
    #[prost(message, repeated, tag = "1")]
    pub file: Vec<File>,
}

#[derive(Clone, PartialEq, Message)]
pub struct ReadRequest {
    #[prost(string, tag = "1")]
    pub path: String,
    #[prost(uint32, tag = "2")]
    pub offset: u32,
    #[prost(uint32, tag = "3")]
    pub size: u32,
}

#[derive(Clone, PartialEq, Message)]
pub struct ReadResponse {
    #[prost(message, optional, tag = "1")]
    pub file: Option<File>,
    #[prost(bool, tag = "2")]
    pub ranged: bool,
}

#[derive(Clone, PartialEq, Message)]
pub struct WriteRequest {
    #[prost(string, tag = "1")]
    pub path: String,
    #[prost(message, optional, tag = "2")]
    pub file: Option<File>,
}

#[derive(Clone, PartialEq, Message)]
pub struct DeleteRequest {
    #[prost(string, tag = "1")]
    pub path: String,
    #[prost(bool, tag = "2")]
    pub recursive: bool,
}

#[derive(Clone, PartialEq, Message)]
pub struct Md5sumResponse {
    #[prost(string, tag = "1")]
    pub md5sum: String,
}

pub const STATUS_OK: i32 = 0;
pub const STATUS_STORAGE_EXIST: i32 = 6;
pub const STATUS_STORAGE_NOT_EXIST: i32 = 7;

/// PB.CommandStatus, in words
pub fn status_text(code: i32) -> &'static str {
    match code {
        0 => "OK",
        1 => "error",
        2 => "the Flipper couldn't decode the request",
        3 => "not implemented",
        4 => "busy",
        5 => "storage not ready (is the SD card in?)",
        6 => "already exists",
        7 => "doesn't exist",
        8 => "invalid parameter",
        9 => "access denied",
        10 => "invalid name",
        11 => "storage internal error",
        12 => "not implemented by the storage",
        13 => "already open",
        14 => "a continuous command was interrupted",
        15 => "invalid parameters",
        16 => "the app can't start",
        17 => "another app is running",
        18 => "the folder isn't empty",
        _ => "unknown status",
    }
}

/// The firmware's UpdatePrepareResult (lib/update_util/update_operation.c), which the RPC update
/// response carries as it is: it has one more value than the .proto's enum
pub fn prepare_result_text(code: i32) -> &'static str {
    match code {
        0 => "OK",
        1 => "Invalid manifest name or location",
        2 => "Update folder not found",
        3 => "Invalid manifest data",
        4 => "Missing Stage2 loader",
        5 => "Corrupted Stage2 loader",
        6 => "Failed to create update pointer file",
        7 => "Update pointer file error (corrupted FS?)",
        8 => "Hardware target mismatch",
        9 => "Update package is too old",
        10 => "Need more free space in internal storage",
        _ => "Unknown error",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn same_bytes_as_device_test_py() {
        // scripts/device_test.py main_msg(3, 19): StopSession, command 3
        let m = Main {
            command_id: 3,
            content: Some(Content::StopSession(Empty {})),
            ..Default::default()
        };
        assert_eq!(
            m.encode_length_delimited_to_vec(),
            vec![0x05, 0x08, 0x03, 0x9a, 0x01, 0x00]
        );
    }

    #[test]
    fn write_request_round_trip() {
        let m = Main {
            command_id: 7,
            has_next: true,
            content: Some(Content::WriteRequest(WriteRequest {
                path: "/ext/update/x/firmware.dfu".into(),
                file: Some(File {
                    data: vec![1, 2, 3],
                    ..Default::default()
                }),
            })),
            ..Default::default()
        };
        let bytes = m.encode_to_vec();
        assert_eq!(Main::decode(&bytes[..]).unwrap(), m);
    }

    #[test]
    fn unknown_content_is_skipped() {
        // a Main carrying gui_screen_frame (22), which this tool doesn't know
        let bytes = [0x08, 0x05, 0xb2, 0x01, 0x02, 0x0a, 0x00];
        let m = Main::decode(&bytes[..]).unwrap();
        assert_eq!(m.command_id, 5);
        assert!(m.content.is_none());
    }
}
