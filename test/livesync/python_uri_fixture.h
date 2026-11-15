#pragma once

// Made by `python3 scripts/livesync_setup_uri.py --write-fixture test/livesync/python_uri_fixture.h`
// (fixed randomness, dummy values only). LiveSyncCryptoTest opens it with the firmware's code, and
// test/test_livesync_setup_uri.py checks this file is what the script still writes.
// clang-format off
namespace pyfixture {
constexpr const char* PASSPHRASE = "dummy-new-passphrase";
constexpr const char* SETUP_URI =
    "obsidian://setuplivesync?settings=%25%24AwoRGB8mLTQ7QklQV15lbHN6gYiPlp2kq7K5wMfO1dwDChEYHyYtNDtC"
    "SVADChEYHyYtNDtCSVBXXmVsc3qBiI%2BWnaSrsrnAx87V3OFOvKemx%2ByO9CQ9K3ppM75s0CMgQ5SxCTeAOC0X7jwZWNC2"
    "8TKODhLLIFMePgAflrjmkWCWMgd1jnt5KYg0nEcrgrhdrp4GoXlDN5cvVTMkivYe8%2FF6htY9PEq5FbIJg6isL8YcJYKv4L"
    "iBt4OrjaUeckYMaM%2B56gA3NckJ3SDNfLqmmSixhQXm0mlEwZDxb1X6mTwAOkzQAORiOc4QztzzR18Iti%2BgWnuMvyVE9T"
    "%2FRAwjYXqZnq43JdN8K11aGLb6hKgG42H1oCFlDklfBTJs76uR%2BXal1rvVs1YkXvPJmPNu2vBBjEuI%3D";
constexpr const char* SETTINGS_JSON =
    "{\"couchDB_URI\":\"https://couch.example.org\",\"couchDB_USER\":\"dummy-user\",\"couchDB_PASSWORD\":\"dummy"
    "-password\",\"couchDB_DBNAME\":\"dummy-vault\",\"encrypt\":true,\"passphrase\":\"dummy-e2ee-passphrase\",\"u"
    "sePathObfuscation\":true}";
}  // namespace pyfixture
// clang-format on
