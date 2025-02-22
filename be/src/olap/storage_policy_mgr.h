#pragma once

#include <stdint.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "common/status.h"

struct StoragePolicy {
    std::string storage_policy_name;
    int64_t cooldown_datetime;
    int64_t cooldown_ttl;
    // s3 resource
    std::string s3_endpoint;
    std::string s3_region;
    std::string s3_ak;
    std::string s3_sk;
    std::string root_path;
    std::string bucket;
    std::string md5_sum;
    int64_t s3_conn_timeout_ms;
    int64_t s3_max_conn;
    int64_t s3_request_timeout_ms;
};

inline std::ostream& operator<<(std::ostream& out, const StoragePolicy& m) {
    out << "storage_policy_name: " << m.storage_policy_name
        << " cooldown_datetime: " << m.cooldown_datetime << " cooldown_ttl: " << m.cooldown_ttl
        << " s3_endpoint: " << m.s3_endpoint << " s3_region: " << m.s3_region
        << " root_path: " << m.root_path << " bucket: " << m.bucket << " md5_sum: " << m.md5_sum
        << " s3_conn_timeout_ms: " << m.s3_conn_timeout_ms << " s3_max_conn: " << m.s3_max_conn
        << " s3_request_timeout_ms: " << m.s3_request_timeout_ms;
    return out;
}

namespace doris {
class ExecEnv;

class StoragePolicyMgr {
public:
    using StoragePolicyPtr = std::shared_ptr<StoragePolicy>;
    StoragePolicyMgr() {}

    ~StoragePolicyMgr() = default;

    // fe push update policy to be
    void update(const std::string& name, StoragePolicyPtr policy);

    // periodic pull from fe
    void periodic_put(const std::string& name, StoragePolicyPtr policy);

    StoragePolicyPtr get(const std::string& name);

    void del(const std::string& name);

private:
    std::mutex _mutex;
    std::unordered_map<std::string, StoragePolicyPtr> _policy_map;
};
} // namespace doris
