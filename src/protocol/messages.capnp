@0xbb29a6f04ade59c8;

using GroupID = UInt32;

struct Group {
    id @0 :GroupID;
    name @1 :Text;
    # A "local" group is one that was resolved using the nss.group_mapping configuration. Its ID is a local group ID
    # and should not be offset.
    local @2 :Bool;
}

using UserID = UInt32;

struct User {
    id @0 :UserID;
    username @1 :Text;
    name @2 :Text;
    # Sorted such that primary group is first
    groups @3 :List(Group);
    state @4 :Text;
}

interface GitLabDaemon {
    getUserByID @0 (id :UserID) -> (errcode :UInt32, user :User);
    getUserByName @1 (name :Text) -> (errcode :UInt32, user :User);
    getSSHKeys @2 (id :UserID) -> (errcode :UInt32, keys :Text);
    getGroupByID @3 (id :GroupID) -> (errcode :UInt32, group :Group);
    getGroupByName @4 (name :Text) -> (errcode :UInt32, group :Group);
}