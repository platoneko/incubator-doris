// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>

#include <memory>

#include "common/config.h"
#include "common/status.h"
#include "gen_cpp/internal_service.pb.h"
#include "io/fs/s3_file_system.h"
#include "olap/delta_writer.h"
#include "olap/rowset/beta_rowset.h"
#include "olap/storage_engine.h"
#include "olap/storage_policy.h"
#include "olap/tablet.h"
#include "runtime/descriptor_helper.h"
#include "runtime/tuple.h"
#include "util/file_utils.h"
#include "util/s3_util.h"

namespace doris {

static StorageEngine* k_engine = nullptr;

static const std::string kTestDir = "./ut_dir/tablet_cooldown_test";
static constexpr int64_t kResourceId = 10000;
static constexpr int64_t kStoragePolicyId = 10002;

// remove DISABLED_ when need run this test
#define TabletCooldownTest DISABLED_TabletCooldownTest
class TabletCooldownTest : public testing::Test {
public:
    static void SetUpTestSuite() {
        S3Conf s3_conf;
        s3_conf.ak = config::test_s3_ak;
        s3_conf.sk = config::test_s3_sk;
        s3_conf.endpoint = config::test_s3_endpoint;
        s3_conf.region = config::test_s3_region;
        s3_conf.bucket = config::test_s3_bucket;
        s3_conf.prefix = config::test_s3_prefix + "/tablet_cooldown_test";
        auto s3_fs = io::S3FileSystem::create(std::move(s3_conf), std::to_string(kResourceId));
        ASSERT_TRUE(s3_fs->connect().ok());
        put_storage_resource(kResourceId, {s3_fs, 1});
        auto storage_policy = std::make_shared<StoragePolicy>();
        storage_policy->name = "TabletCooldownTest";
        storage_policy->version = 1;
        storage_policy->resource_id = kResourceId;
        put_storage_policy(kStoragePolicyId, storage_policy);

        constexpr uint32_t MAX_PATH_LEN = 1024;
        char buffer[MAX_PATH_LEN];
        EXPECT_NE(getcwd(buffer, MAX_PATH_LEN), nullptr);
        config::storage_root_path = std::string(buffer) + "/" + kTestDir;
        config::min_file_descriptor_number = 1000;

        FileUtils::remove_all(config::storage_root_path);
        FileUtils::create_dir(config::storage_root_path);

        std::vector<StorePath> paths {{config::storage_root_path, -1}};

        EngineOptions options;
        options.store_paths = paths;
        doris::StorageEngine::open(options, &k_engine);
    }

    static void TearDownTestSuite() {
        if (k_engine != nullptr) {
            k_engine->stop();
            delete k_engine;
            k_engine = nullptr;
        }
    }
};

static void create_tablet_request_with_sequence_col(int64_t tablet_id, int32_t schema_hash,
                                                    TCreateTabletReq* request) {
    request->tablet_id = tablet_id;
    request->__set_version(1);
    request->tablet_schema.schema_hash = schema_hash;
    request->tablet_schema.short_key_column_count = 2;
    request->tablet_schema.keys_type = TKeysType::UNIQUE_KEYS;
    request->tablet_schema.storage_type = TStorageType::COLUMN;
    request->tablet_schema.__set_sequence_col_idx(2);
    request->__set_storage_format(TStorageFormat::V2);

    TColumn k1;
    k1.column_name = "k1";
    k1.__set_is_key(true);
    k1.column_type.type = TPrimitiveType::TINYINT;
    request->tablet_schema.columns.push_back(k1);

    TColumn k2;
    k2.column_name = "k2";
    k2.__set_is_key(true);
    k2.column_type.type = TPrimitiveType::SMALLINT;
    request->tablet_schema.columns.push_back(k2);

    TColumn sequence_col;
    sequence_col.column_name = SEQUENCE_COL;
    sequence_col.__set_is_key(false);
    sequence_col.column_type.type = TPrimitiveType::INT;
    sequence_col.__set_aggregation_type(TAggregationType::REPLACE);
    request->tablet_schema.columns.push_back(sequence_col);

    TColumn v1;
    v1.column_name = "v1";
    v1.__set_is_key(false);
    v1.column_type.type = TPrimitiveType::DATETIME;
    v1.__set_aggregation_type(TAggregationType::REPLACE);
    request->tablet_schema.columns.push_back(v1);
}

static TDescriptorTable create_descriptor_tablet_with_sequence_col() {
    TDescriptorTableBuilder desc_tbl_builder;
    TTupleDescriptorBuilder tuple_builder;

    tuple_builder.add_slot(
            TSlotDescriptorBuilder().type(TYPE_TINYINT).column_name("k1").column_pos(0).build());
    tuple_builder.add_slot(
            TSlotDescriptorBuilder().type(TYPE_SMALLINT).column_name("k2").column_pos(1).build());
    tuple_builder.add_slot(TSlotDescriptorBuilder()
                                   .type(TYPE_INT)
                                   .column_name(SEQUENCE_COL)
                                   .column_pos(2)
                                   .build());
    tuple_builder.add_slot(
            TSlotDescriptorBuilder().type(TYPE_DATETIME).column_name("v1").column_pos(3).build());
    tuple_builder.build(&desc_tbl_builder);

    return desc_tbl_builder.desc_tbl();
}

TEST_F(TabletCooldownTest, normal) {
    // create tablet
    TCreateTabletReq request;
    create_tablet_request_with_sequence_col(10005, 270068377, &request);
    Status st = k_engine->create_tablet(request);
    ASSERT_EQ(Status::OK(), st);

    TDescriptorTable tdesc_tbl = create_descriptor_tablet_with_sequence_col();
    ObjectPool obj_pool;
    DescriptorTbl* desc_tbl = nullptr;
    DescriptorTbl::create(&obj_pool, tdesc_tbl, &desc_tbl);
    TupleDescriptor* tuple_desc = desc_tbl->get_tuple_descriptor(0);

    // write data
    PUniqueId load_id;
    load_id.set_hi(0);
    load_id.set_lo(0);
    WriteRequest write_req = {10005, 270068377, WriteType::LOAD, 20003,
                              30003, load_id,   tuple_desc,      &(tuple_desc->slots())};
    DeltaWriter* delta_writer = nullptr;
    DeltaWriter::open(&write_req, &delta_writer);
    ASSERT_NE(delta_writer, nullptr);

    vectorized::Block block;
    for (const auto& slot_desc : tuple_desc->slots()) {
        block.insert(vectorized::ColumnWithTypeAndName(slot_desc->get_empty_mutable_column(),
                                                       slot_desc->get_data_type_ptr(),
                                                       slot_desc->col_name()));
    }

    auto columns = block.mutate_columns();
    {
        int8_t c1 = 123;
        columns[0]->insert_data((const char*)&c1, sizeof(c1));

        int16_t c2 = 456;
        columns[1]->insert_data((const char*)&c2, sizeof(c2));

        int32_t c3 = 1;
        columns[2]->insert_data((const char*)&c3, sizeof(c2));

        DateTimeValue c4;
        c4.from_date_str("2020-07-16 19:39:43", 19);
        int64_t c4_int = c4.to_int64();
        columns[3]->insert_data((const char*)&c4_int, sizeof(c4));

        st = delta_writer->write(&block, {0});
        ASSERT_EQ(Status::OK(), st);
    }

    st = delta_writer->close();
    ASSERT_EQ(Status::OK(), st);
    st = delta_writer->close_wait(PSlaveTabletNodes(), false);
    ASSERT_EQ(Status::OK(), st);
    delete delta_writer;

    // publish version success
    TabletSharedPtr tablet =
            k_engine->tablet_manager()->get_tablet(write_req.tablet_id, write_req.schema_hash);
    OlapMeta* meta = tablet->data_dir()->get_meta();
    Version version;
    version.first = tablet->rowset_with_max_version()->end_version() + 1;
    version.second = tablet->rowset_with_max_version()->end_version() + 1;
    std::map<TabletInfo, RowsetSharedPtr> tablet_related_rs;
    StorageEngine::instance()->txn_manager()->get_txn_related_tablets(
            write_req.txn_id, write_req.partition_id, &tablet_related_rs);
    for (auto& tablet_rs : tablet_related_rs) {
        RowsetSharedPtr rowset = tablet_rs.second;
        st = k_engine->txn_manager()->publish_txn(meta, write_req.partition_id, write_req.txn_id,
                                                  tablet->tablet_id(), tablet->schema_hash(),
                                                  tablet->tablet_uid(), version);
        ASSERT_EQ(Status::OK(), st);
        st = tablet->add_inc_rowset(rowset);
        ASSERT_EQ(Status::OK(), st);
    }
    EXPECT_EQ(1, tablet->num_rows());

    // test cooldown
    tablet->set_storage_policy_id(kStoragePolicyId);
    st = tablet->cooldown(); // rowset [0-1]
    ASSERT_EQ(Status::OK(), st);
    st = tablet->cooldown(); // rowset [2-2]
    ASSERT_EQ(Status::OK(), st);
    auto rs = tablet->get_rowset_by_version({2, 2});
    ASSERT_FALSE(rs->is_local());

    // test read
    ASSERT_EQ(Status::OK(), st);
    std::vector<segment_v2::SegmentSharedPtr> segments;
    st = std::static_pointer_cast<BetaRowset>(rs)->load_segments(&segments);
    ASSERT_EQ(Status::OK(), st);
    ASSERT_EQ(segments.size(), 1);
}

} // namespace doris
